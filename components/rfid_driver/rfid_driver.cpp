#include "rfid_driver.hpp"

#include <cstring>

#include <esp_log.h>

static const char *TAG = "RfidLock";

RfidLock::RfidLock(spi_host_device_t host, gpio_num_t cs, gpio_num_t rst)
    : _host(host), _cs(cs), _rst(rst), _driver(nullptr), _scanner(nullptr),
      _notifyTask(nullptr), _stateMutex(nullptr), _lastSak(0),
      _cardPresent(false) {
  memset(&_lastUid, 0, sizeof(_lastUid));
  _stateMutex = xSemaphoreCreateMutex();
}

RfidLock::~RfidLock() {
  end();
  if (_stateMutex) {
    vSemaphoreDelete(_stateMutex);
  }
}

esp_err_t RfidLock::begin() {
  rc522_spi_config_t driverConfig = {};

  driverConfig.host_id = _host;
  driverConfig.dev_config.spics_io_num = _cs;
  driverConfig.rst_io_num = _rst;

  esp_err_t err = rc522_spi_create(&driverConfig, &_driver);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Blad tworzenia drivera SPI rc522: %d", err);
    return err;
  }

  err = rc522_driver_install(_driver);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Blad instalacji drivera rc522: %d", err);
    _driver = nullptr;
    return err;
  }

  rc522_config_t scannerConfig = {};
  scannerConfig.driver = _driver;

  err = rc522_create(&scannerConfig, &_scanner);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Blad tworzenia skanera rc522: %d", err);
    rc522_driver_uninstall(_driver);
    _driver = nullptr;
    return err;
  }

  err = rc522_register_events(_scanner, RC522_EVENT_PICC_STATE_CHANGED,
                              &RfidLock::onPiccStateChangedThunk, this);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Blad rejestracji handlera zdarzen rc522: %d", err);
    rc522_destroy(_scanner);
    _scanner = nullptr;
    rc522_driver_uninstall(_driver);
    _driver = nullptr;
    return err;
  }

  err = rc522_start(_scanner);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Blad startu skanera rc522: %d", err);
    rc522_unregister_events(_scanner, RC522_EVENT_PICC_STATE_CHANGED,
                            &RfidLock::onPiccStateChangedThunk);
    rc522_destroy(_scanner);
    _scanner = nullptr;
    rc522_driver_uninstall(_driver);
    _driver = nullptr;
    return err;
  }

  ESP_LOGI(TAG, "RfidLock zainicjalizowany");
  return ESP_OK;
}

void RfidLock::end() {
  if (_scanner) {
    rc522_pause(_scanner);
    rc522_unregister_events(_scanner, RC522_EVENT_PICC_STATE_CHANGED,
                            &RfidLock::onPiccStateChangedThunk);
    rc522_destroy(_scanner);
    _scanner = nullptr;
  }
  if (_driver) {
    rc522_driver_uninstall(_driver);
    _driver = nullptr;
  }
}

void RfidLock::attachNotifyTask(TaskHandle_t task) { _notifyTask = task; }

size_t RfidLock::getUid(char *out, size_t outLen) const {
  static const char kHexDigits[] = "0123456789ABCDEF";

  xSemaphoreTake(_stateMutex, portMAX_DELAY);
  uint8_t length = _cardPresent ? _lastUid.length : 0;
  uint8_t value[RC522_PICC_UID_SIZE_MAX];
  memcpy(value, _lastUid.value, sizeof(value));
  xSemaphoreGive(_stateMutex);

  if (outLen == 0) {
    return length;
  }

  size_t maxBytes = (outLen - 1) / 2;
  size_t toCopy = (length < maxBytes) ? length : maxBytes;

  for (size_t i = 0; i < toCopy; i++) {
    out[i * 2] = kHexDigits[value[i] >> 4];
    out[i * 2 + 1] = kHexDigits[value[i] & 0x0F];
  }
  out[toCopy * 2] = '\0';

  return length;
}

void RfidLock::clearUid() {
  xSemaphoreTake(_stateMutex, portMAX_DELAY);
  memset(&_lastUid, 0, sizeof(_lastUid));
  _lastSak = 0;
  _cardPresent = false;
  xSemaphoreGive(_stateMutex);
}

void RfidLock::onPiccStateChangedThunk(void *arg, esp_event_base_t base,
                                       int32_t eventId, void *eventData) {
  (void)base;
  (void)eventId;
  static_cast<RfidLock *>(arg)->onPiccStateChanged(
      static_cast<rc522_picc_state_changed_event_t *>(eventData));
}

void RfidLock::onPiccStateChanged(rc522_picc_state_changed_event_t *event) {
  rc522_picc_t *picc = event->picc;

  if (picc->state == RC522_PICC_STATE_ACTIVE) {
    // Kopiujemy uid+sak TERAZ, bo picc jest wskaznikiem wewnetrznym
    // biblioteki - patrz komentarz klasy o czasie zycia event->picc.
    xSemaphoreTake(_stateMutex, portMAX_DELAY);
    _lastUid = picc->uid;
    _lastSak = picc->sak;
    _cardPresent = true;
    xSemaphoreGive(_stateMutex);

    ESP_LOGI(TAG, "Karta wykryta, dlugosc UID=%u", picc->uid.length);
    if (_notifyTask) {
      xTaskNotify(_notifyTask, static_cast<uint32_t>(RfidEvent::CARD_PRESENT),
                  eSetBits);
    }
  } else if (picc->state == RC522_PICC_STATE_IDLE &&
             event->old_state >= RC522_PICC_STATE_ACTIVE) {
    xSemaphoreTake(_stateMutex, portMAX_DELAY);
    _cardPresent = false;
    xSemaphoreGive(_stateMutex);

    ESP_LOGI(TAG, "Karta usunieta");
    if (_notifyTask) {
      xTaskNotify(_notifyTask, static_cast<uint32_t>(RfidEvent::CARD_REMOVED),
                  eSetBits);
    }
  }
}

esp_err_t RfidLock::readBlock(uint8_t sector, uint8_t blockInSector,
                              uint8_t out[kBlockSize], const uint8_t key[6]) {
  rc522_picc_t picc = {};
  xSemaphoreTake(_stateMutex, portMAX_DELAY);
  bool present = _cardPresent;
  picc.uid = _lastUid;
  picc.sak = _lastSak;
  xSemaphoreGive(_stateMutex);

  if (!present) {
    return ESP_ERR_INVALID_STATE;
  }

  uint8_t blockAddress = sector * 4 + blockInSector;

  rc522_mifare_key_t mifareKey = {};
  mifareKey.type = RC522_MIFARE_KEY_A;
  memcpy(mifareKey.value, key, sizeof(mifareKey.value));

  esp_err_t err = rc522_mifare_auth(_scanner, &picc, blockAddress, &mifareKey);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "Autoryzacja bloku %u nieudana: %d", blockAddress, err);
    return err;
  }

  err = rc522_mifare_read(_scanner, &picc, blockAddress, out);
  rc522_mifare_deauth(_scanner, &picc);
  return err;
}

esp_err_t RfidLock::writeBlock(uint8_t sector, uint8_t blockInSector,
                               const uint8_t data[kBlockSize],
                               const uint8_t key[6]) {
  rc522_picc_t picc = {};
  xSemaphoreTake(_stateMutex, portMAX_DELAY);
  bool present = _cardPresent;
  picc.uid = _lastUid;
  picc.sak = _lastSak;
  xSemaphoreGive(_stateMutex);

  if (!present) {
    return ESP_ERR_INVALID_STATE;
  }

  uint8_t blockAddress = sector * 4 + blockInSector;

  rc522_mifare_key_t mifareKey = {};
  mifareKey.type = RC522_MIFARE_KEY_A;
  memcpy(mifareKey.value, key, sizeof(mifareKey.value));

  esp_err_t err = rc522_mifare_auth(_scanner, &picc, blockAddress, &mifareKey);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "Autoryzacja bloku %u nieudana: %d", blockAddress, err);
    return err;
  }

  err = rc522_mifare_write(_scanner, &picc, blockAddress, data);
  rc522_mifare_deauth(_scanner, &picc);
  return err;
}
