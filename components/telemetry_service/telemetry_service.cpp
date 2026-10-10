#include "telemetry_service.hpp"

#include <cstdio>
#include <cstring>

#include <cJSON.h>
#include <esp_log.h>
#include <esp_system.h>

extern const uint8_t mqtt_ca_pem_start[] asm("_binary_mqtt_ca_pem_start");
extern const uint8_t mqtt_ca_pem_end[] asm("_binary_mqtt_ca_pem_end");

static const char *TAG = "TelemetryService";

namespace {
constexpr uint64_t kStateIntervalUs = 60ULL * 1000000ULL;        // 60s
constexpr uint64_t kDiagnosticsIntervalUs = 300ULL * 1000000ULL; // 300s

esp_err_t createTimer(esp_timer_cb_t callback, void *arg, const char *name,
                      esp_timer_handle_t *out) {
  esp_timer_create_args_t args = {};
  args.callback = callback;
  args.arg = arg;
  args.name = name;
  return esp_timer_create(&args, out);
}
} // namespace

TelemetryService::TelemetryService(const char *brokerUri, const char *deviceId,
                                   const char *mqttUsername,
                                   const char *mqttPassword,
                                   QueueHandle_t telemetryQueue,
                                   QueueHandle_t commandQueue)
    : _telemetryQueue(telemetryQueue), _commandQueue(commandQueue),
      _client(nullptr), _taskHandle(nullptr), _lastKnownLocked(true),
      _haveLastKnownLocked(false), _stateIntervalTimer(nullptr),
      _diagnosticsIntervalTimer(nullptr), _statePending(false),
      _diagnosticsPending(false) {
  memset(_brokerUri, 0, sizeof(_brokerUri));
  memset(_deviceId, 0, sizeof(_deviceId));
  memset(_mqttUsername, 0, sizeof(_mqttUsername));
  memset(_mqttPassword, 0, sizeof(_mqttPassword));

  strncpy(_brokerUri, brokerUri, sizeof(_brokerUri) - 1);
  strncpy(_deviceId, deviceId, sizeof(_deviceId) - 1);
  strncpy(_mqttUsername, mqttUsername, sizeof(_mqttUsername) - 1);
  strncpy(_mqttPassword, mqttPassword, sizeof(_mqttPassword) - 1);

  snprintf(_topicState, sizeof(_topicState), "smartlock/%s/state", _deviceId);
  snprintf(_topicEvent, sizeof(_topicEvent), "smartlock/%s/event", _deviceId);
  snprintf(_topicDiagnostics, sizeof(_topicDiagnostics),
           "smartlock/%s/diagnostics", _deviceId);
  snprintf(_topicCommand, sizeof(_topicCommand), "smartlock/%s/command",
           _deviceId);
}

TelemetryService::~TelemetryService() { end(); }

esp_err_t TelemetryService::begin(int coreId) {
  esp_mqtt_client_config_t mqttCfg = {};
  mqttCfg.broker.address.uri = _brokerUri;
  // TLS (mqtts://) - weryfikacja certyfikatu brokera wzgledem naszego CA
  // (certs/mqtt_ca.pem, wpiety przez EMBED_TXTFILES).
  mqttCfg.broker.verification.certificate =
      reinterpret_cast<const char *>(mqtt_ca_pem_start);
  mqttCfg.broker.verification.certificate_len =
      static_cast<size_t>(mqtt_ca_pem_end - mqtt_ca_pem_start);

  mqttCfg.credentials.username = _mqttUsername;
  mqttCfg.credentials.authentication.password = _mqttPassword;

  _client = esp_mqtt_client_init(&mqttCfg);
  if (!_client) {
    ESP_LOGE(TAG, "esp_mqtt_client_init nieudane");
    return ESP_FAIL;
  }

  esp_err_t err = esp_mqtt_client_register_event(
      _client, static_cast<esp_mqtt_event_id_t>(ESP_EVENT_ANY_ID),
      &TelemetryService::mqttEventHandlerThunk, this);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Blad rejestracji handlera zdarzen MQTT: %d", err);
    esp_mqtt_client_destroy(_client);
    _client = nullptr;
    return err;
  }

  err = createTimer(&TelemetryService::stateIntervalCallback, this,
                    "telemetry_state", &_stateIntervalTimer);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Blad tworzenia timera state: %d", err);
    esp_mqtt_client_destroy(_client);
    _client = nullptr;
    return err;
  }
  esp_timer_start_periodic(_stateIntervalTimer, kStateIntervalUs);

  err = createTimer(&TelemetryService::diagnosticsIntervalCallback, this,
                    "telemetry_diag", &_diagnosticsIntervalTimer);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Blad tworzenia timera diagnostics: %d", err);
    esp_timer_stop(_stateIntervalTimer);
    esp_timer_delete(_stateIntervalTimer);
    _stateIntervalTimer = nullptr;
    esp_mqtt_client_destroy(_client);
    _client = nullptr;
    return err;
  }
  esp_timer_start_periodic(_diagnosticsIntervalTimer, kDiagnosticsIntervalUs);

  // Rdzen 0 (domyslnie) - w odroznieniu od reszty aplikacji, ktora zyje na
  // rdzeniu 1 (patrz komentarz klasy w telemetry_service.hpp).
  BaseType_t ok = xTaskCreatePinnedToCore(taskThunk, "telemetry_service_task",
                                          6144, this, 3, &_taskHandle, coreId);
  if (ok != pdPASS) {
    ESP_LOGE(TAG, "Nie udalo sie utworzyc taska TelemetryService");
    end();
    return ESP_ERR_NO_MEM;
  }

  err = esp_mqtt_client_start(_client);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Blad esp_mqtt_client_start: %d", err);
    end();
    return err;
  }

  ESP_LOGI(TAG,
           "TelemetryService uruchomiony (rdzen %d), broker: %s, device_id: %s",
           coreId, _brokerUri, _deviceId);
  return ESP_OK;
}

void TelemetryService::end() {
  if (_stateIntervalTimer) {
    esp_timer_stop(_stateIntervalTimer);
    esp_timer_delete(_stateIntervalTimer);
    _stateIntervalTimer = nullptr;
  }
  if (_diagnosticsIntervalTimer) {
    esp_timer_stop(_diagnosticsIntervalTimer);
    esp_timer_delete(_diagnosticsIntervalTimer);
    _diagnosticsIntervalTimer = nullptr;
  }
  if (_taskHandle) {
    vTaskDelete(_taskHandle);
    _taskHandle = nullptr;
  }
  if (_client) {
    esp_mqtt_client_stop(_client);
    esp_mqtt_client_destroy(_client);
    _client = nullptr;
  }
}

void TelemetryService::stateIntervalCallback(void *arg) {
  static_cast<TelemetryService *>(arg)->_statePending = true;
}

void TelemetryService::diagnosticsIntervalCallback(void *arg) {
  static_cast<TelemetryService *>(arg)->_diagnosticsPending = true;
}

void TelemetryService::taskThunk(void *pParam) {
  static_cast<TelemetryService *>(pParam)->taskLoop();
}

void TelemetryService::taskLoop() {
  while (true) {
    TelemetryMessage msg;
    if (xQueueReceive(_telemetryQueue, &msg, pdMS_TO_TICKS(1000)) == pdTRUE) {
      switch (msg.type) {
      case TelemetryMessageType::STATE_UPDATE:
        _lastKnownLocked = msg.locked;
        _haveLastKnownLocked = true;
        publishStateSnapshot(); // publikacja NATYCHMIAST na zmiane stanu
        break;
      case TelemetryMessageType::EVENT_LOG:
        publishEvent(msg);
        break;
      case TelemetryMessageType::DIAGNOSTICS:
        publishDiagnostics();
        break;
      }
    }

    if (_statePending) {
      _statePending = false;
      if (_haveLastKnownLocked) {
        publishStateSnapshot();
      }
    }
    if (_diagnosticsPending) {
      _diagnosticsPending = false;
      publishDiagnostics();
    }
  }
}

void TelemetryService::publishJson(const char *topic, cJSON *root, int qos,
                                   int retain) {
  if (!root) {
    ESP_LOGE(TAG, "cJSON_CreateObject zwrocil NULL - pomijam publikacje na %s",
             topic);
    return;
  }

  char *payload = cJSON_PrintUnformatted(root);
  if (!payload) {
    ESP_LOGE(TAG,
             "cJSON_PrintUnformatted zwrocil NULL - pomijam publikacje na %s",
             topic);
    cJSON_Delete(root);
    return;
  }

  if (_client) {
    int msgId =
        esp_mqtt_client_publish(_client, topic, payload, 0, qos, retain);
    if (msgId < 0) {
      ESP_LOGW(TAG, "Publikacja na %s nieudana", topic);
    }
  }

  // cJSON_PrintUnformatted alokuje przez wewnetrzny alokator cJSON - do
  // zwolnienia sluzy cJSON_free (nie zwykle free()), bo respektuje
  // ewentualne wlasne hooki ustawione przez cJSON_InitHooks.
  cJSON_free(payload);
  cJSON_Delete(root);
}

void TelemetryService::publishStateSnapshot() {
  cJSON *root = cJSON_CreateObject();
  if (root) {
    cJSON_AddStringToObject(root, "device_id", _deviceId);
    cJSON_AddStringToObject(root, "state",
                            _lastKnownLocked ? "LOCKED" : "UNLOCKED");
    cJSON_AddNumberToObject(root, "timestamp_ms",
                            static_cast<double>(esp_timer_get_time() / 1000));
  }
  publishJson(_topicState, root, 1 /*QoS1*/, 1 /*retain*/);
}

void TelemetryService::publishEvent(const TelemetryMessage &msg) {
  cJSON *root = cJSON_CreateObject();
  if (root) {
    cJSON_AddStringToObject(root, "device_id", _deviceId);
    cJSON_AddStringToObject(root, "event", msg.eventName);
    cJSON_AddStringToObject(root, "detail", msg.eventDetail);
    cJSON_AddNumberToObject(root, "timestamp_ms",
                            static_cast<double>(msg.timestampMs));
  }
  publishJson(_topicEvent, root, 1 /*QoS1*/, 0 /*retain*/);
}

void TelemetryService::publishDiagnostics() {
  cJSON *root = cJSON_CreateObject();
  if (root) {
    cJSON_AddStringToObject(root, "device_id", _deviceId);
    cJSON_AddNumberToObject(root, "free_heap_bytes", esp_get_free_heap_size());
    cJSON_AddNumberToObject(root, "min_free_heap_bytes",
                            esp_get_minimum_free_heap_size());
    cJSON_AddNumberToObject(
        root, "uptime_s", static_cast<double>(esp_timer_get_time() / 1000000));
  }
  publishJson(_topicDiagnostics, root, 0 /*QoS0*/, 0 /*retain*/);
}

void TelemetryService::mqttEventHandlerThunk(void *arg, esp_event_base_t base,
                                             int32_t eventId, void *eventData) {
  (void)base;
  (void)eventId;
  static_cast<TelemetryService *>(arg)->onMqttEvent(
      static_cast<esp_mqtt_event_handle_t>(eventData));
}

void TelemetryService::onMqttEvent(esp_mqtt_event_handle_t event) {
  switch (event->event_id) {
  case MQTT_EVENT_CONNECTED:
    ESP_LOGI(TAG, "Polaczono z brokerem MQTT");
    esp_mqtt_client_subscribe(_client, _topicCommand, 1 /*QoS1*/);
    _statePending = true; // natychmiastowy snapshot po (re)polaczeniu
    break;

  case MQTT_EVENT_DISCONNECTED:
    ESP_LOGW(TAG, "Rozlaczono z brokerem MQTT");
    break;

  case MQTT_EVENT_DATA:
    // event->topic NIE jest zakonczone '\0' - porownujemy dlugosc i
    // zawartosc razem, zeby nie dopasowac przypadkiem dluzszego topicu
    // majacego _topicCommand jako prefiks.
    if (event->topic_len > 0 &&
        static_cast<size_t>(event->topic_len) == strlen(_topicCommand) &&
        strncmp(event->topic, _topicCommand, event->topic_len) == 0) {
      handleIncomingCommand(event->data, event->data_len);
    }
    break;

  default:
    break;
  }
}

void TelemetryService::handleIncomingCommand(const char *data, int dataLen) {
  // event->data NIE jest zakonczone '\0' - kopiujemy do lokalnego bufora
  // przed parsowaniem przez cJSON_Parse (ktore oczekuje C-stringa).
  char buffer[128];
  int toCopy = (dataLen < static_cast<int>(sizeof(buffer)) - 1)
                   ? dataLen
                   : static_cast<int>(sizeof(buffer)) - 1;
  if (toCopy < 0) {
    toCopy = 0;
  }
  memcpy(buffer, data, toCopy);
  buffer[toCopy] = '\0';

  cJSON *root = cJSON_Parse(buffer);
  if (!root) {
    ESP_LOGW(TAG, "Niepoprawny JSON w komendzie: %s", buffer);
    return;
  }

  SystemCommand cmd = {};
  cmd.id = SystemCommandId::UNKNOWN;
  cmd.parameter[0] = '\0';

  // Kopiujemy nazwe komendy do lokalnego bufora PRZED cJSON_Delete() -
  // wskaznik cmdField->valuestring wskazuje do wewnatrz drzewa cJSON i
  // staje sie niewazny (use-after-free) natychmiast po jego usunieciu,
  // a chcemy jeszcze zalogowac ta nazwe w komunikacie ponizej.
  char cmdName[16] = "?";

  const cJSON *cmdField = cJSON_GetObjectItemCaseSensitive(root, "cmd");
  if (cJSON_IsString(cmdField) && cmdField->valuestring) {
    strncpy(cmdName, cmdField->valuestring, sizeof(cmdName) - 1);

    if (strcmp(cmdField->valuestring, "UNLOCK") == 0) {
      cmd.id = SystemCommandId::UNLOCK;
    } else if (strcmp(cmdField->valuestring, "LOCK") == 0) {
      cmd.id = SystemCommandId::LOCK;
    } else if (strcmp(cmdField->valuestring, "REBOOT") == 0) {
      cmd.id = SystemCommandId::REBOOT;
    }
  }

  const cJSON *paramField = cJSON_GetObjectItemCaseSensitive(root, "param");
  if (cJSON_IsString(paramField) && paramField->valuestring) {
    strncpy(cmd.parameter, paramField->valuestring, sizeof(cmd.parameter) - 1);
  }

  cJSON_Delete(root); // cJSON_Parse alokuje cale drzewo - zawsze zwalniamy

  if (cmd.id == SystemCommandId::UNKNOWN) {
    ESP_LOGW(TAG, "Nieznana/niepoprawna komenda w JSON: %s", buffer);
    return;
  }

  if (xQueueSend(_commandQueue, &cmd, 0) != pdTRUE) {
    ESP_LOGW(TAG, "commandQueue pelna - komenda '%s' odrzucona", cmdName);
  }
}
