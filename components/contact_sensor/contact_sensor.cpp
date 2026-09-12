#include "contact_sensor.hpp"

#include <esp_log.h>

static const char *TAG = "ContactSensor";

namespace {
constexpr int kDebounceMs = 30;
}

ContactSensor::ContactSensor(gpio_num_t pin)
    : _pin(pin), _taskHandle(nullptr), _notifyTask(nullptr), _isrQueue(nullptr),
      _stateMutex(nullptr), _isOpen(false) {
  _stateMutex = xSemaphoreCreateMutex();
}

ContactSensor::~ContactSensor() {
  end();
  if (_stateMutex) {
    vSemaphoreDelete(_stateMutex);
  }
}

esp_err_t ContactSensor::begin() {
  gpio_config_t cfg = {};
  cfg.pin_bit_mask = 1ULL << _pin;
  cfg.mode = GPIO_MODE_INPUT;
  cfg.intr_type = GPIO_INTR_ANYEDGE;
  cfg.pull_up_en = GPIO_PULLUP_DISABLE;
  cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;

  esp_err_t err = gpio_config(&cfg);
  if (err != ESP_OK) {
    err = gpio_config(&cfg);
    if (err != ESP_OK) {
      ESP_LOGE(TAG, "Blad konfiguracji GPIO%d: %d", static_cast<int>(_pin),
               err);
      return err;
    }
  }

  _isrQueue = xQueueCreate(4, sizeof(uint8_t));
  if (!_isrQueue) {
    return ESP_ERR_NO_MEM;
  }

  esp_err_t isrServiceErr = gpio_install_isr_service(ESP_INTR_FLAG_EDGE);
  if (isrServiceErr != ESP_OK && isrServiceErr != ESP_ERR_INVALID_STATE) {
    ESP_LOGE(TAG, "Blad instalacji serwisu ISR GPIO: %d", isrServiceErr);
    vQueueDelete(_isrQueue);
    _isrQueue = nullptr;
    return isrServiceErr;
  }

  err = gpio_isr_handler_add(_pin, isrHandler, this);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Blad dodania ISR dla GPIO%d: %d", static_cast<int>(_pin),
             err);
    vQueueDelete(_isrQueue);
    _isrQueue = nullptr;
    return err;
  }

  // Odczyt stanu poczatkowego bez czekania na pierwsze zbocze.
  _isOpen = gpio_get_level(_pin) != 0;

  BaseType_t ok = xTaskCreate(taskThunk, "contact_sensor_task", 2048, this, 5,
                              &_taskHandle);
  if (ok != pdPASS) {
    ESP_LOGE(TAG, "Nie udalo sie utworzyc taska ContactSensor");
    gpio_isr_handler_remove(_pin);
    vQueueDelete(_isrQueue);
    _isrQueue = nullptr;
    return ESP_ERR_NO_MEM;
  }

  ESP_LOGI(TAG, "ContactSensor (GPIO%d) zainicjalizowany, stan poczatkowy: %s",
           static_cast<int>(_pin), _isOpen ? "otwarte" : "zamkniete");
  return ESP_OK;
}

void ContactSensor::end() {
  if (_taskHandle) {
    vTaskDelete(_taskHandle);
    _taskHandle = nullptr;
  }
  gpio_isr_handler_remove(_pin);
  if (_isrQueue) {
    vQueueDelete(_isrQueue);
    _isrQueue = nullptr;
  }
}

void ContactSensor::attachNotifyTask(TaskHandle_t task) { _notifyTask = task; }

bool ContactSensor::isOpen() const {
  xSemaphoreTake(_stateMutex, portMAX_DELAY);
  bool state = _isOpen;
  xSemaphoreGive(_stateMutex);
  return state;
}

void IRAM_ATTR ContactSensor::isrHandler(void *arg) {
  ContactSensor *self = static_cast<ContactSensor *>(arg);
  uint8_t dummy = 0;
  BaseType_t higherPriorityTaskWoken = pdFALSE;
  xQueueSendFromISR(self->_isrQueue, &dummy, &higherPriorityTaskWoken);
  if (higherPriorityTaskWoken) {
    portYIELD_FROM_ISR();
  }
}

void ContactSensor::taskThunk(void *pParam) {
  static_cast<ContactSensor *>(pParam)->taskLoop();
}

void ContactSensor::taskLoop() {
  uint8_t dummy;
  while (true) {
    if (xQueueReceive(_isrQueue, &dummy, portMAX_DELAY) != pdTRUE) {
      continue;
    }

    vTaskDelay(pdMS_TO_TICKS(kDebounceMs));

    // HIGH = otwarte (kontaktron rozwarty, podciagniety do 3.3V), LOW =
    // zamkniete (magnes zwiera do GND).
    bool newState = gpio_get_level(_pin) != 0;

    bool changed = false;
    xSemaphoreTake(_stateMutex, portMAX_DELAY);
    if (newState != _isOpen) {
      _isOpen = newState;
      changed = true;
    }
    xSemaphoreGive(_stateMutex);

    if (!changed) {
      continue; // odbicie styku / zaklocenie - stan sie nie zmienil
    }

    ESP_LOGI(TAG, "Kontaktron: %s", newState ? "otwarte" : "zamkniete");
    if (_notifyTask) {
      xTaskNotify(_notifyTask,
                  static_cast<uint32_t>(newState ? ContactEvent::OPENED
                                                 : ContactEvent::CLOSED),
                  eSetBits);
    }
  }
}
