#include "access_controller.hpp"
#include "contact_sensor.hpp"
#include "diagnostic_manager.hpp"
#include "display.hpp"
#include "esp_log.h"
#include "esp_netif_sntp.h"
#include "esp_sntp.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "keypad_driver.hpp"
#include "nvs_flash.h"
#include "presence_sensor.hpp"
#include "pressure_sensor.hpp"
#include "rfid_driver.hpp"
#include "servo.hpp"
#include "smart_led.hpp"
#include "telemetry_service.hpp"
#include "touch_sensor.hpp"
#include "wifi_manager.hpp"
#include <driver/spi_master.h>

#include "pin_config.hpp"

static const char *_tag = "SmartLockMain";

static const char *kWifiSsid = "WiezaWawrzyny2.4G";
static const char *kWifiPassword = "Wawrzyny19";

static const char *kMqttUsername = "mqtt";
static const char *kMqttPassword = "12345";

static const char *kMqttBrokerUri = "mqtts://10.0.0.71:8883";
static const char *kDeviceId = "smartlock01";
static const char *kVisionDeviceId = "vision01";

static KeypadLock keypad(Board::Keypad::pins.data());
static RfidLock rfid(Board::Spi::host, Board::Rfid::cs, Board::Rfid::rst);
static Display display(Board::Spi::host, Board::LCD::cs, Board::LCD::dc,
                       Board::LCD::rst, Board::LCD::width, Board::LCD::height);
static ContactSensor contactSensor(Board::Contact::pin);
static SmartLed ledGreen(Board::Led::status_green);
static SmartLed ledRed(Board::Led::status_red);
static PresenceSensor presenceSensor(Board::PIR_sensor::pin);
static TouchSensor touchSensor(Board::Touch::touch);
static PressureSensor pressureSensor(Board::AnalogPressure::pin);
static ServoMotor lockServo(Board::SERVO::zamek);
static WifiManager wifiManager(kWifiSsid, kWifiPassword);
static SystemDiagnostics diagnostics;

static QueueHandle_t telemetryQueue =
    xQueueCreate(10, sizeof(TelemetryMessage));
static QueueHandle_t commandQueue = xQueueCreate(5, sizeof(SystemCommand));

static TelemetryService telemetryService(kMqttBrokerUri, kDeviceId,
                                         kMqttUsername, kMqttPassword,
                                         telemetryQueue, commandQueue,
                                         kVisionDeviceId);

static AccessController accessController(keypad, rfid, contactSensor,
                                         presenceSensor, touchSensor,
                                         pressureSensor, lockServo, display,
                                         ledGreen, ledRed, telemetryQueue,
                                         commandQueue);

static esp_err_t initSharedSpiBus() {
  spi_bus_config_t busConfig = {}; // utwórz pustą strukturę i wyzeruj pola
  busConfig.mosi_io_num = Board::Spi::mosi;
  busConfig.miso_io_num = Board::Spi::miso;
  busConfig.sclk_io_num = Board::Spi::sck;

  // nie używam Quad - write protect i hold;
  busConfig.quadwp_io_num = GPIO_NUM_NC;
  busConfig.quadhd_io_num = GPIO_NUM_NC;

  return spi_bus_initialize(Board::Spi::host, &busConfig, SPI_DMA_DISABLED);
}

// rozwiązanei problemu łączenia C i C++, ESP-IDF napsiany w C, brak mangling;
// kompilacja według reguł C, aby OS potrafił odnaleźć i uruchomić
extern "C" void app_main(void) {
  ESP_LOGI(_tag, "Uruchamianie systemu Smart Lock...");

  diagnostics.start(5000, 1, 1);

  // NVS wymagane przez WiFi (esp_wifi_init zapisuje/odczytuje kalibracje).
  esp_err_t nvsErr = nvs_flash_init();
  if (nvsErr == ESP_ERR_NVS_NO_FREE_PAGES ||
      nvsErr == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    nvsErr = nvs_flash_init();
  }
  ESP_ERROR_CHECK(nvsErr);

  esp_err_t err = wifiManager.begin();
  if (err != ESP_OK) {
    ESP_LOGE(_tag, "Nie udalo sie uruchomic WifiManager: %d", err);
  }

  esp_sntp_config_t sntpCfg = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
  esp_netif_sntp_init(&sntpCfg);
  esp_netif_sntp_sync_wait(
      pdMS_TO_TICKS(10000)); // czekaj do 10s na synchronizacje

  // Rdzen 0 - zostaje razem z WiFi (patrz komentarz klasy w
  // telemetry_service.hpp), w odroznieniu od reszty aplikacji ponizej.
  err = telemetryService.begin(0);
  if (err != ESP_OK) {
    ESP_LOGE(_tag, "Nie udalo sie uruchomic TelemetryService: %d", err);
  }

  err = initSharedSpiBus();
  if (err != ESP_OK) {
    ESP_LOGE(_tag, "Nie udalo sie zainicjalizowac magistrali SPI: %d", err);
  }

  err = rfid.begin();
  if (err != ESP_OK) {
    ESP_LOGE(_tag, "Nie udalo sie uruchomic RfidLock: %d", err);
  }

  err = keypad.begin();
  if (err != ESP_OK) {
    ESP_LOGE(_tag, "Nie udalo sie uruchomic KeypadLock: %d", err);
  }

  err = display.begin();
  if (err != ESP_OK) {
    ESP_LOGE(_tag, "Nie udalo sie uruchomic Display: %d", err);
  } else {
    display.showIdle(true); // poczatkowy stan - zablokowany
  }

  err = contactSensor.begin();
  if (err != ESP_OK) {
    ESP_LOGE(_tag, "Nie udalo sie uruchomic ContactSensor: %d", err);
  }

  err = presenceSensor.begin();
  if (err != ESP_OK) {
    ESP_LOGE(_tag, "Nie udalo sie uruchomic PresenceSensor: %d", err);
  }

  err = touchSensor.begin();
  if (err != ESP_OK) {
    ESP_LOGE(_tag, "Nie udalo sie uruchomic TouchSensor: %d", err);
  }

  err = pressureSensor.begin();
  if (err != ESP_OK) {
    ESP_LOGE(_tag, "Nie udalo sie uruchomic PressureSensor: %d", err);
  }

  err = lockServo.begin();
  if (err != ESP_OK) {
    ESP_LOGE(_tag, "Nie udalo sie uruchomic ServoMotor: %d", err);
  }

  err = accessController.begin();
  if (err != ESP_OK) {
    ESP_LOGE(_tag, "Nie udalo sie uruchomic AccessController: %d", err);
  }

  ESP_LOGI(_tag, "System zamka gotowy");
}