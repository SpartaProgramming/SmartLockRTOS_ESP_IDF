#include "smart_led.hpp"
#include "esp_log.h"

static const char *kTag = "SmartLed";

SmartLed::SmartLed(gpio_num_t pin)
    : _pin(pin), _ledState(false), _currentState(LedState::OFF),
      _blinkIntervalMs(1000), _commandQueue(nullptr), _taskHandle(nullptr) {

  gpio_reset_pin(_pin);
  gpio_set_direction(_pin, GPIO_MODE_OUTPUT);
  gpio_set_level(_pin, 0);

  _commandQueue = xQueueCreate(5, sizeof(LedCommand));

  if (_commandQueue == nullptr) {
    ESP_LOGE(kTag, "Failed to create command queue");
  }
  if (xTaskCreate(taskThunk, "smart_led_task", 2048, this, 5, &_taskHandle) !=
      pdPASS) {
    ESP_LOGE(kTag, "Failed to create task");
  }

  ESP_LOGI(kTag, "Initialized SmartLed on GPIO pin %d", _pin);
}

void SmartLed::taskThunk(void *_pParam) {
  SmartLed *_pThis = static_cast<SmartLed *>(_pParam);
  _pThis->taskLoop();
}

void SmartLed::taskLoop() {
  LedCommand _receivedCmd;

  while (1) {
    TickType_t _timeout = portMAX_DELAY;
    if (_currentState == LedState::BLINK) {
      _timeout = pdMS_TO_TICKS(_blinkIntervalMs);
    }

    if (xQueueReceive(_commandQueue, &_receivedCmd, _timeout) == pdTRUE) {
      _currentState = _receivedCmd.state;
      _blinkIntervalMs = _receivedCmd.intervalMs;

      if (_currentState == LedState::ON) {
        _ledState = true;
        gpio_set_level(_pin, 1);
        ESP_LOGI(kTag, "Stan zmieniony: WŁĄCZONA na stałe");
      } else if (_currentState == LedState::OFF) {
        _ledState = false;
        gpio_set_level(_pin, 0);
        ESP_LOGI(kTag, "Stan zmieniony: WYŁĄCZONA na stałe");
      } else if (_currentState == LedState::BLINK) {
        ESP_LOGI(kTag, "Stan zmieniony: MIGANIE z interwałem %lu ms",
                 _blinkIntervalMs);
        _ledState = true;
        gpio_set_level(_pin, 1);
      }
    } else {
      if (_currentState == LedState::BLINK) {
        _ledState = !_ledState;
        gpio_set_level(_pin, _ledState ? 1 : 0);
      }
    }
  }
}

void SmartLed::turnOn() {
  LedCommand _cmd = {LedState::ON, 0};
  xQueueSend(_commandQueue, &_cmd, 0);
}

void SmartLed::turnOff() {
  LedCommand _cmd = {LedState::OFF, 0};
  xQueueSend(_commandQueue, &_cmd, 0);
}

void SmartLed::blink(uint32_t intervalMs) {
  LedCommand _cmd = {LedState::BLINK, intervalMs};
  xQueueSend(_commandQueue, &_cmd, 0);
}

SmartLed::~SmartLed() {
  if (_taskHandle) {
    vTaskDelete(_taskHandle);
  }
  if (_commandQueue) {
    vQueueDelete(_commandQueue);
  }
  gpio_reset_pin(_pin);
}