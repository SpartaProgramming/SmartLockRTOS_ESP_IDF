#pragma once

#include <cstdint>

#include <driver/gpio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <esp_adc/adc_cali.h>
#include <esp_adc/adc_oneshot.h>

enum class PressureEvent : uint32_t {
  NONE = 0,
  PRESSED = 1u << 10,
  RELEASED = 1u << 11,
};

class PressureSensor {
public:
  explicit PressureSensor(gpio_num_t pin);
  ~PressureSensor();

  PressureSensor(const PressureSensor &) = delete;
  PressureSensor &operator=(const PressureSensor &) = delete;
  PressureSensor(PressureSensor &&) = delete;
  PressureSensor &operator=(PressureSensor &&) = delete;

  esp_err_t begin();

  void end();

  void attachNotifyTask(TaskHandle_t task);

  int readMillivolts() const;

  int readRaw() const;

  bool isPressed() const;

  void setThresholdMv(int thresholdMv);

  // pressed - odczyt poniżej progu
  void setPressedBelowThreshold(bool pressedBelowThreshold);

private:
  static void taskThunk(void *pParam);
  void taskLoop();
  int sampleAveragedRaw();
  int rawToMv(int raw) const;
  void initCalibration();

  gpio_num_t _pin;
  adc_unit_t _unit;
  adc_channel_t _channel;
  adc_oneshot_unit_handle_t _adcHandle;
  adc_cali_handle_t _caliHandle;
  bool _caliEnabled;

  TaskHandle_t _taskHandle;
  TaskHandle_t _notifyTask;
  SemaphoreHandle_t _stateMutex;

  int _lastRaw;
  int _lastMv;
  bool _isPressed;
  int _thresholdMv;
  bool _pressedBelowThreshold;
};
