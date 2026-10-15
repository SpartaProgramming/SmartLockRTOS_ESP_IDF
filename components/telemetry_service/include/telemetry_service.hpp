#pragma once

#include <cstdint>

#include <esp_err.h>
#include <esp_event.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

#include <mqtt_client.h>

#include "telemetry_types.hpp"

struct cJSON;

class TelemetryService {
public:
  explicit TelemetryService(const char *brokerUri, const char *deviceId,
                            const char *mqttUsername, const char *mqttPassword,
                            QueueHandle_t telemetryQueue,
                            QueueHandle_t commandQueue,
                            const char *visionDeviceId = nullptr);
  ~TelemetryService();

  TelemetryService(const TelemetryService &) = delete;
  TelemetryService &operator=(const TelemetryService &) = delete;
  TelemetryService(TelemetryService &&) = delete;
  TelemetryService &operator=(TelemetryService &&) = delete;

  esp_err_t begin(int coreId = 0);

  void end();

private:
  static void taskThunk(void *pParam);
  void taskLoop();

  static void mqttEventHandlerThunk(void *arg, esp_event_base_t base,
                                    int32_t eventId, void *eventData);
  void onMqttEvent(esp_mqtt_event_handle_t event);

  void publishStateSnapshot();
  void publishDiagnostics();
  void publishEvent(const TelemetryMessage &msg);
  void handleIncomingCommand(const char *data, int dataLen);
  /// Zdarzenia z kamery (smartlock/<vision_id>/event) - "FACE_RECOGNIZED"
  /// zamieniamy na SystemCommandId::FACE_RECOGNIZED w commandQueue.
  void handleVisionEvent(const char *data, int dataLen);

  void publishJson(const char *topic, cJSON *root, int qos, int retain);

  static void stateIntervalCallback(void *arg);
  static void diagnosticsIntervalCallback(void *arg);

  char _brokerUri[128];
  char _deviceId[32];
  char _topicState[64];
  char _topicEvent[64];
  char _topicDiagnostics[64];
  char _topicCommand[64];
  char _topicVisionEvent[64]; // "" = brak kamery, nie subskrybujemy

  char _mqttUsername[33];
  char _mqttPassword[65];

  QueueHandle_t _telemetryQueue;
  QueueHandle_t _commandQueue;

  esp_mqtt_client_handle_t _client;
  TaskHandle_t _taskHandle;

  bool _lastKnownLocked;
  bool _haveLastKnownLocked;

  esp_timer_handle_t _stateIntervalTimer;       // okresowy, co 60s
  esp_timer_handle_t _diagnosticsIntervalTimer; // okresowy, co 300s

  volatile bool _statePending;
  volatile bool _diagnosticsPending;
};
