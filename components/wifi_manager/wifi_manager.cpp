#include "wifi_manager.hpp"

#include <cstdio>
#include <cstring>

#include <esp_log.h>
#include <esp_wifi.h>

#include "access_events.hpp"

#if __has_include("secrets.hpp") // sprawdza czy plik nagłówkowy istnieje i może
                                 // być dołączony
#else
#error                                                                         \
    "Brak main/secrets.hpp - skopiuj secrets.example.h jako secrets.hpp i uzupelnij"
#endif

static const char *TAG = "WifiManager";

bool WifiManager::s_instanceExists = false;

WifiManager::WifiManager(const char *ssid, const char *password)
    : _netif(nullptr), _wifiEventInstance(nullptr), _ipEventInstance(nullptr),
      _reconnectTaskHandle(nullptr), _notifyTask(nullptr),
      _reconnectSignal(nullptr), _stateMutex(nullptr),
      _state(WifiManagerState::DISCONNECTED), _reconnectAttempt(0) {
  memset(_ssid, 0, sizeof(_ssid));
  memset(_password, 0, sizeof(_password));
  strncpy(_ssid, ssid, sizeof(_ssid) - 1);
  strncpy(_password, password, sizeof(_password) - 1);
  memset(_ipAddress, 0, sizeof(_ipAddress));

  _stateMutex = xSemaphoreCreateMutex();
  _reconnectSignal = xSemaphoreCreateBinary();
}

WifiManager::~WifiManager() {
  end();
  if (_stateMutex) {
    vSemaphoreDelete(_stateMutex);
  }
  if (_reconnectSignal) {
    vSemaphoreDelete(_reconnectSignal);
  }
}

esp_err_t WifiManager::begin() {
  if (s_instanceExists) {
    ESP_LOGE(TAG,
             "WifiManager juz dziala gdzie indziej - WiFi to zasob "
             "systemowy (singleton), nie mozna go zainicjalizowac dwa razy");
    return ESP_ERR_INVALID_STATE;
  }

  esp_err_t err = esp_netif_init();
  if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
    ESP_LOGE(TAG, "Blad esp_netif_init: %d", err);
    return err;
  }

  err = esp_event_loop_create_default();
  if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
    ESP_LOGE(TAG, "Blad esp_event_loop_create_default: %d", err);
    return err;
  }

  _netif = esp_netif_create_default_wifi_sta();
  if (!_netif) {
    ESP_LOGE(TAG, "Blad tworzenia netif STA");
    return ESP_FAIL;
  }

  wifi_init_config_t initCfg = WIFI_INIT_CONFIG_DEFAULT();
  err = esp_wifi_init(&initCfg);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Blad esp_wifi_init: %d", err);
    esp_netif_destroy_default_wifi(_netif);
    _netif = nullptr;
    return err;
  }

  err = esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                            &WifiManager::eventHandlerThunk,
                                            this, &_wifiEventInstance);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Blad rejestracji handlera WIFI_EVENT: %d", err);
    esp_wifi_deinit();
    esp_netif_destroy_default_wifi(_netif);
    _netif = nullptr;
    return err;
  }

  err = esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                            &WifiManager::eventHandlerThunk,
                                            this, &_ipEventInstance);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Blad rejestracji handlera IP_EVENT: %d", err);
    esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                          _wifiEventInstance);
    _wifiEventInstance = nullptr;
    esp_wifi_deinit();
    esp_netif_destroy_default_wifi(_netif);
    _netif = nullptr;
    return err;
  }

  wifi_config_t wifiConfig = {};
  strncpy(reinterpret_cast<char *>(wifiConfig.sta.ssid), _ssid,
          sizeof(wifiConfig.sta.ssid) - 1);
  strncpy(reinterpret_cast<char *>(wifiConfig.sta.password), _password,
          sizeof(wifiConfig.sta.password) - 1);
  wifiConfig.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

  err = esp_wifi_set_mode(WIFI_MODE_STA);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Blad esp_wifi_set_mode: %d", err);
    return err;
  }

  err = esp_wifi_set_config(WIFI_IF_STA, &wifiConfig);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Blad esp_wifi_set_config: %d", err);
    return err;
  }

  BaseType_t ok =
      xTaskCreatePinnedToCore(reconnectTaskThunk, "wifi_reconnect_task", 3072,
                              this, 4, &_reconnectTaskHandle, 1);
  if (ok != pdPASS) {
    ESP_LOGE(TAG, "Nie udalo sie utworzyc taska rekonekcji WiFi");
    return ESP_ERR_NO_MEM;
  }

  err = esp_wifi_start();
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Blad esp_wifi_start: %d", err);
    vTaskDelete(_reconnectTaskHandle);
    _reconnectTaskHandle = nullptr;
    return err;
  }

  s_instanceExists = true;
  ESP_LOGI(TAG, "WifiManager zainicjalizowany, laczenie z SSID '%s'...", _ssid);
  return ESP_OK;
}

void WifiManager::end() {
  if (_reconnectTaskHandle) {
    vTaskDelete(_reconnectTaskHandle);
    _reconnectTaskHandle = nullptr;
  }
  if (_wifiEventInstance) {
    esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                          _wifiEventInstance);
    _wifiEventInstance = nullptr;
  }
  if (_ipEventInstance) {
    esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                          _ipEventInstance);
    _ipEventInstance = nullptr;
  }
  esp_wifi_stop();
  esp_wifi_deinit();
  if (_netif) {
    esp_netif_destroy_default_wifi(_netif);
    _netif = nullptr;
  }
  s_instanceExists = false;
}

void WifiManager::attachNotifyTask(TaskHandle_t task) { _notifyTask = task; }

WifiManagerState WifiManager::getState() const {
  xSemaphoreTake(_stateMutex, portMAX_DELAY);
  WifiManagerState s = _state;
  xSemaphoreGive(_stateMutex);
  return s;
}

bool WifiManager::getIpAddress(char *out, size_t outLen) const {
  if (outLen == 0) {
    return false;
  }
  xSemaphoreTake(_stateMutex, portMAX_DELAY);
  bool connected = (_state == WifiManagerState::CONNECTED);
  size_t len = strlen(_ipAddress);
  size_t toCopy = (len < outLen - 1) ? len : outLen - 1;
  memcpy(out, _ipAddress, toCopy);
  out[toCopy] = '\0';
  xSemaphoreGive(_stateMutex);
  return connected;
}

void WifiManager::eventHandlerThunk(void *arg, esp_event_base_t base,
                                    int32_t id, void *data) {
  static_cast<WifiManager *>(arg)->onWifiEvent(base, id, data);
}

void WifiManager::onWifiEvent(esp_event_base_t base, int32_t id, void *data) {
  if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
    ESP_LOGI(TAG, "WiFi STA wystartowane - laczenie...");
    xSemaphoreTake(_stateMutex, portMAX_DELAY);
    _state = WifiManagerState::CONNECTING;
    xSemaphoreGive(_stateMutex);
    esp_wifi_connect();
  } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
    ESP_LOGW(TAG, "WiFi rozlaczone");
    xSemaphoreTake(_stateMutex, portMAX_DELAY);
    _state = WifiManagerState::DISCONNECTED;
    memset(_ipAddress, 0, sizeof(_ipAddress));
    xSemaphoreGive(_stateMutex);

    if (_notifyTask) {
      xTaskNotify(_notifyTask, AccessEvents::kWifiDisconnected, eSetBits);
    }
    xSemaphoreGive(_reconnectSignal);

  } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
    auto *event = static_cast<ip_event_got_ip_t *>(data);
    xSemaphoreTake(_stateMutex, portMAX_DELAY);
    _state = WifiManagerState::CONNECTED;
    _reconnectAttempt = 0;
    snprintf(_ipAddress, sizeof(_ipAddress), IPSTR, IP2STR(&event->ip_info.ip));
    xSemaphoreGive(_stateMutex);

    ESP_LOGI(TAG, "Polaczono, IP: %s", _ipAddress);
    if (_notifyTask) {
      xTaskNotify(_notifyTask, AccessEvents::kWifiConnected, eSetBits);
    }
  }
}

void WifiManager::reconnectTaskThunk(void *pParam) {
  static_cast<WifiManager *>(pParam)->reconnectTaskLoop();
}

void WifiManager::reconnectTaskLoop() {
  while (true) {
    if (xSemaphoreTake(_reconnectSignal, portMAX_DELAY) != pdTRUE) {
      continue;
    }

    uint32_t shift = (_reconnectAttempt < kMaxBackoffShift) ? _reconnectAttempt
                                                            : kMaxBackoffShift;
    uint32_t delayMs = kReconnectBaseDelayMs << shift;
    if (delayMs > kReconnectMaxDelayMs) {
      delayMs = kReconnectMaxDelayMs;
    }
    _reconnectAttempt++;

    ESP_LOGI(TAG, "Ponowne polaczenie za %u ms (proba %u)",
             static_cast<unsigned>(delayMs),
             static_cast<unsigned>(_reconnectAttempt));
    vTaskDelay(pdMS_TO_TICKS(delayMs));

    esp_err_t err = esp_wifi_connect();
    if (err != ESP_OK && err != ESP_ERR_WIFI_CONN) {
      ESP_LOGW(TAG, "esp_wifi_connect nieudane (%d) - kolejna proba po backoff",
               err);
      xSemaphoreGive(_reconnectSignal);
    }
  }
}