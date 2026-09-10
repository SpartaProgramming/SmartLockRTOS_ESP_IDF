#pragma once

#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

// Stany operacyjne diody
enum class LedState { OFF, ON, BLINK };

// Struktura polecenia przesyłana przez kolejkę
struct LedCommand {
  LedState state;
  uint32_t intervalMs;
};

class SmartLed {
private:
  gpio_num_t _pin;
  bool _ledState;
  LedState _currentState;
  uint32_t _blinkIntervalMs;

  QueueHandle_t _commandQueue;
  TaskHandle_t _taskHandle;

  // Statyczny thunk (mostek z C do C++)
  static void taskThunk(void *_pParam);

  // Active Object
  void taskLoop();

  SmartLed(const SmartLed &) = delete;
  SmartLed &operator=(const SmartLed &) = delete;
  SmartLed(SmartLed &&) = delete;
  SmartLed &operator=(SmartLed &&) = delete;

public:
  explicit SmartLed(gpio_num_t pin);
  ~SmartLed();

  void turnOn();
  void turnOff();
  void blink(uint32_t intervalMs);
};