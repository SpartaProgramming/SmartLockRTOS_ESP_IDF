#pragma once

#include <cstddef>
#include <cstdint>

#include <esp_err.h>
#include <esp_event.h>
#include <esp_netif.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

enum class WifiManagerState {
  DISCONNECTED,
  CONNECTING,
  CONNECTED,
};

class WifiManager {
public:
  WifiManager(const char *ssid, const char *password);
  ~WifiManager();

  WifiManager(const WifiManager &) = delete;
  WifiManager &operator=(const WifiManager &) = delete;
  WifiManager(WifiManager &&) = delete;
  WifiManager &operator=(WifiManager &&) = delete;

  esp_err_t begin();
  void end();
  void attachNotifyTask(TaskHandle_t task);

  WifiManagerState getState() const;
  bool getIpAddress(char *out, size_t outLen) const;

private:
  static constexpr uint32_t kReconnectBaseDelayMs = 1000;
  static constexpr uint32_t kReconnectMaxDelayMs = 30000;
  static constexpr uint32_t kMaxBackoffShift = 16;
  static void eventHandlerThunk(void *arg, esp_event_base_t base, int32_t id,
                                void *data);
  void onWifiEvent(esp_event_base_t base, int32_t id, void *data);

  static void reconnectTaskThunk(void *pParam);
  void reconnectTaskLoop();

  char _ssid[33];
  char _password[65];

  esp_netif_t *_netif;
  esp_event_handler_instance_t _wifiEventInstance;
  esp_event_handler_instance_t _ipEventInstance;

  TaskHandle_t _reconnectTaskHandle;
  TaskHandle_t _notifyTask;
  SemaphoreHandle_t _reconnectSignal;

  mutable SemaphoreHandle_t _stateMutex;
  WifiManagerState _state;
  uint32_t _reconnectAttempt;
  char _ipAddress[16];

  static bool s_instanceExists;
};