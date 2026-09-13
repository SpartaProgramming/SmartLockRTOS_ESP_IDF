#pragma once

#include <cstdint>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

class SystemDiagnostics {
public:
  SystemDiagnostics() = default;
  ~SystemDiagnostics();

  // Blokada kopiowania instancji
  SystemDiagnostics(const SystemDiagnostics &) = delete;
  SystemDiagnostics &operator=(const SystemDiagnostics &) = delete;

  bool start(uint32_t intervalMs = 60000, UBaseType_t priority = 1,
             BaseType_t coreId = 1);

  void stop();

private:
  static void taskThunk(void *pvParameters);
  void taskLoop();
  void printReport();

  TaskHandle_t _taskHandle{nullptr};
  uint32_t _intervalMs{5000};
  bool _isRunning{false};
};