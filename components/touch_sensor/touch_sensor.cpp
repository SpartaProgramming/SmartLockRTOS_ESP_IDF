#include "touch_sensor.hpp"

#include <esp_log.h>

static const char *TAG = "TouchSensor";

namespace {
constexpr int kDebounceMs = 30;
}

TouchSensor::TouchSensor(gpio_num_t pin)
    : _pin(pin), _taskHandle(nullptr), _notifyTask(nullptr),
      _isrQueue(nullptr), _stateMutex(nullptr), _isTouched(false) {
  _stateMutex = xSemaphoreCreateMutex();
}

TouchSensor::~TouchSensor() {
  end();
  if (_stateMutex) {
    vSemaphoreDelete(_stateMutex);
  }
}

esp_err_t TouchSensor::begin() {
  gpio_config_t cfg = {};
  cfg.pin_bit_mask = 1ULL << _pin;
  cfg.mode = GPIO_MODE_INPUT;
  cfg.intr_type = GPIO_INTR_ANYEDGE;
  // TTP223B ma wlasny aktywny stopien wyjsciowy - bez pull-up/pull-down,
  // patrz komentarz klasy.
  cfg.pull_up_en = GPIO_PULLUP_DISABLE;
  cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;

  esp_err_t err = gpio_config(&cfg);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Blad konfiguracji GPIO%d: %d", static_cast<int>(_pin), err);
    return err;
  }

  _isrQueue = xQueueCreate(4, sizeof(uint8_t));
  if (!_isrQueue) {
    return ESP_ERR_NO_MEM;
  }

  // Wspolny serwis ISR moze byc juz zainstalowany przez inny driver (np.
  // keypad_driver/contact_sensor/presence_sensor) - to oczekiwane i
  // nieszkodliwe.
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
  _isTouched = gpio_get_level(_pin) != 0;

  BaseType_t ok = xTaskCreate(taskThunk, "touch_sensor_task", 2048, this, 5,
                              &_taskHandle);
  if (ok != pdPASS) {
    ESP_LOGE(TAG, "Nie udalo sie utworzyc taska TouchSensor");
    gpio_isr_handler_remove(_pin);
    vQueueDelete(_isrQueue);
    _isrQueue = nullptr;
    return ESP_ERR_NO_MEM;
  }

  ESP_LOGI(TAG, "TouchSensor (GPIO%d) zainicjalizowany, stan poczatkowy: %s",
           static_cast<int>(_pin), _isTouched ? "dotyk" : "brak");
  return ESP_OK;
}

void TouchSensor::end() {
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

void TouchSensor::attachNotifyTask(TaskHandle_t task) { _notifyTask = task; }

bool TouchSensor::isTouched() const {
  xSemaphoreTake(_stateMutex, portMAX_DELAY);
  bool state = _isTouched;
  xSemaphoreGive(_stateMutex);
  return state;
}

void IRAM_ATTR TouchSensor::isrHandler(void *arg) {
  TouchSensor *self = static_cast<TouchSensor *>(arg);
  uint8_t dummy = 0;
  BaseType_t higherPriorityTaskWoken = pdFALSE;
  xQueueSendFromISR(self->_isrQueue, &dummy, &higherPriorityTaskWoken);
  if (higherPriorityTaskWoken) {
    portYIELD_FROM_ISR();
  }
}

void TouchSensor::taskThunk(void *pParam) {
  static_cast<TouchSensor *>(pParam)->taskLoop();
}

void TouchSensor::taskLoop() {
  uint8_t dummy;
  while (true) {
    if (xQueueReceive(_isrQueue, &dummy, portMAX_DELAY) != pdTRUE) {
      continue;
    }

    vTaskDelay(pdMS_TO_TICKS(kDebounceMs));

    bool newState = gpio_get_level(_pin) != 0;

    bool changed = false;
    xSemaphoreTake(_stateMutex, portMAX_DELAY);
    if (newState != _isTouched) {
      _isTouched = newState;
      changed = true;
    }
    xSemaphoreGive(_stateMutex);

    if (!changed) {
      continue;
    }

    ESP_LOGI(TAG, "Dotyk: %s", newState ? "wykryto" : "zwolniono");
    if (_notifyTask) {
      xTaskNotify(_notifyTask,
                  static_cast<uint32_t>(newState ? TouchEvent::TOUCHED
                                                 : TouchEvent::RELEASED),
                  eSetBits);
    }
  }
}
