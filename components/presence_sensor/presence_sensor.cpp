#include "presence_sensor.hpp"

#include <esp_log.h>

static const char *TAG = "PresenceSensor";

namespace {
constexpr int kDebounceMs = 30;
}

PresenceSensor::PresenceSensor(gpio_num_t pin)
    : _pin(pin), _taskHandle(nullptr), _notifyTask(nullptr),
      _isrQueue(nullptr), _stateMutex(nullptr), _isPresent(false) {
  _stateMutex = xSemaphoreCreateMutex();
}

PresenceSensor::~PresenceSensor() {
  end();
  if (_stateMutex) {
    vSemaphoreDelete(_stateMutex);
  }
}

esp_err_t PresenceSensor::begin() {
  gpio_config_t cfg = {};
  cfg.pin_bit_mask = 1ULL << _pin;
  cfg.mode = GPIO_MODE_INPUT;
  cfg.intr_type = GPIO_INTR_ANYEDGE;
  // Modul PIR ma wlasny aktywny stopien wyjsciowy - bez pull-up/pull-down,
  // w przeciwienstwie do biernego kontaktronu (patrz komentarz klasy).
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
  // keypad_driver/contact_sensor) - to oczekiwane i nieszkodliwe.
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
  _isPresent = gpio_get_level(_pin) != 0;

  BaseType_t ok = xTaskCreate(taskThunk, "presence_sensor_task", 2048, this, 5,
                              &_taskHandle);
  if (ok != pdPASS) {
    ESP_LOGE(TAG, "Nie udalo sie utworzyc taska PresenceSensor");
    gpio_isr_handler_remove(_pin);
    vQueueDelete(_isrQueue);
    _isrQueue = nullptr;
    return ESP_ERR_NO_MEM;
  }

  ESP_LOGI(TAG,
           "PresenceSensor (GPIO%d) zainicjalizowany, stan poczatkowy: %s",
           static_cast<int>(_pin), _isPresent ? "obecnosc" : "brak");
  return ESP_OK;
}

void PresenceSensor::end() {
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

void PresenceSensor::attachNotifyTask(TaskHandle_t task) {
  _notifyTask = task;
}

bool PresenceSensor::isPresent() const {
  xSemaphoreTake(_stateMutex, portMAX_DELAY);
  bool state = _isPresent;
  xSemaphoreGive(_stateMutex);
  return state;
}

void IRAM_ATTR PresenceSensor::isrHandler(void *arg) {
  PresenceSensor *self = static_cast<PresenceSensor *>(arg);
  uint8_t dummy = 0;
  BaseType_t higherPriorityTaskWoken = pdFALSE;
  xQueueSendFromISR(self->_isrQueue, &dummy, &higherPriorityTaskWoken);
  if (higherPriorityTaskWoken) {
    portYIELD_FROM_ISR();
  }
}

void PresenceSensor::taskThunk(void *pParam) {
  static_cast<PresenceSensor *>(pParam)->taskLoop();
}

void PresenceSensor::taskLoop() {
  uint8_t dummy;
  while (true) {
    if (xQueueReceive(_isrQueue, &dummy, portMAX_DELAY) != pdTRUE) {
      continue;
    }

    vTaskDelay(pdMS_TO_TICKS(kDebounceMs));

    bool newState = gpio_get_level(_pin) != 0;

    bool changed = false;
    xSemaphoreTake(_stateMutex, portMAX_DELAY);
    if (newState != _isPresent) {
      _isPresent = newState;
      changed = true;
    }
    xSemaphoreGive(_stateMutex);

    if (!changed) {
      continue;
    }

    ESP_LOGI(TAG, "PIR: %s", newState ? "wykryto obecnosc" : "brak obecnosci");
    if (_notifyTask) {
      xTaskNotify(_notifyTask,
                  static_cast<uint32_t>(newState ? PresenceEvent::DETECTED
                                                 : PresenceEvent::CLEARED),
                  eSetBits);
    }
  }
}
