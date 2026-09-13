#include "diagnostic_manager.hpp"
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_system.h>

static const char *TAG = "DIAGNOSTICS";

SystemDiagnostics::~SystemDiagnostics() { stop(); }

bool SystemDiagnostics::start(uint32_t intervalMs, UBaseType_t priority,
                              BaseType_t coreId) {
  if (_isRunning) {
    ESP_LOGW(TAG, "Zadanie diagnostyczne juz dziala.");
    return false;
  }

  _intervalMs = intervalMs;
  _isRunning = true;

  BaseType_t result =
      xTaskCreatePinnedToCore(&SystemDiagnostics::taskThunk, "sys_diag_task",
                              3072, this, priority, &_taskHandle, coreId);

  if (result != pdPASS) {
    ESP_LOGE(TAG, "Nie udalo sie utworzyc zadania diagnostycznego");
    _isRunning = false;
    return false;
  }

  ESP_LOGI(TAG, "Uruchomiono moduł diagnostyczny (interwał: %lu ms)",
           _intervalMs);
  return true;
}

void SystemDiagnostics::stop() {
  if (_isRunning && _taskHandle != nullptr) {
    _isRunning = false;
    vTaskDelete(_taskHandle);
    _taskHandle = nullptr;
    ESP_LOGI(TAG, "Zatrzmano moduł diagnostyczny.");
  }
}

void SystemDiagnostics::taskThunk(void *pvParameters) {
  auto *self = static_cast<SystemDiagnostics *>(pvParameters);
  self->taskLoop();
}

void SystemDiagnostics::taskLoop() {
  TickType_t lastWakeTime = xTaskGetTickCount();

  while (_isRunning) {
    printReport();
    vTaskDelayUntil(&lastWakeTime, pdMS_TO_TICKS(_intervalMs));
  }

  vTaskDelete(NULL);
}

void SystemDiagnostics::printReport() {
  // 1. Pomiar wolnej pamieci Heap (RAM)
  size_t totalFree = esp_get_free_heap_size();
  size_t minFree = esp_get_minimum_free_heap_size();

  // SRAM (Wewnętrzna pamieć RAM)
  size_t internalFree = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
  size_t internalMaxBlock =
      heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);

  // PSRAM (Zewnętrzna pamięć, jeśli dostępna)
  size_t psramFree = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);

  // 2. Zużycie stosu diagnostycznego
  UBaseType_t diagStackRemaining =
      uxTaskGetStackHighWaterMark(NULL) * sizeof(StackType_t);

  ESP_LOGI(TAG, "==================== STATUS SYSTEMU ====================");
  ESP_LOGI(TAG, "RAM Total Free    : %6u B (%4u KB)", totalFree,
           totalFree / 1024);
  ESP_LOGI(TAG, "RAM Minimum Free  : %6u B (%4u KB)", minFree, minFree / 1024);
  ESP_LOGI(TAG, "SRAM Internal Free: %6u B (%4u KB)", internalFree,
           internalFree / 1024);
  ESP_LOGI(TAG, "SRAM Max Block    : %6u B (%4u KB)", internalMaxBlock,
           internalMaxBlock / 1024);
  ESP_LOGI(TAG, "=======================================================");
}