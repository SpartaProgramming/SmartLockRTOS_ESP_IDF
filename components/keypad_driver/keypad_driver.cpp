#include "keypad_driver.hpp"

#include <cstring>
#include <esp_log.h>

static const char *TAG = "KeypadLock";

KeypadLock::KeypadLock(const gpio_num_t pins[8])
    : _taskHandle(nullptr), _notifyTask(nullptr), _bufferMutex(nullptr),
      _bufferLen(0) {
  memcpy(_pins, pins, sizeof(_pins));
  memset(_buffer, 0, sizeof(_buffer));
  _bufferMutex = xSemaphoreCreateMutex();
}

KeypadLock::~KeypadLock() {
  end();
  if (_bufferMutex) {
    vSemaphoreDelete(_bufferMutex);
  }
}

esp_err_t KeypadLock::begin() {
  esp_err_t err = keypad_initalize(_pins);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Blad inicjalizacji drivera klawiatury: %d", err);
    return err;
  }

  BaseType_t ok =
      xTaskCreate(taskThunk, "keypad_lock_task", 3072, this, 5, &_taskHandle);
  if (ok != pdPASS) {
    ESP_LOGE(TAG, "Nie udalo sie utworzyc taska KeypadLock");
    keypad_delete();
    return ESP_ERR_NO_MEM;
  }

  ESP_LOGI(TAG, "KeypadLock zainicjalizowany");
  return ESP_OK;
}

void KeypadLock::end() {
  if (_taskHandle) {
    vTaskDelete(_taskHandle);
    _taskHandle = nullptr;
    keypad_delete();
  }
}

void KeypadLock::attachNotifyTask(TaskHandle_t task) { _notifyTask = task; }

size_t KeypadLock::getBuffer(char *out, size_t outLen) const {
  size_t len = 0;
  xSemaphoreTake(_bufferMutex, portMAX_DELAY);
  len = _bufferLen;
  size_t toCopy = (len < outLen - 1) ? len : outLen - 1;
  memcpy(out, _buffer, toCopy);
  out[toCopy] = '\0';
  xSemaphoreGive(_bufferMutex);
  return len;
}

void KeypadLock::clearBuffer() {
  xSemaphoreTake(_bufferMutex, portMAX_DELAY);
  _bufferLen = 0;
  memset(_buffer, 0, sizeof(_buffer));
  xSemaphoreGive(_bufferMutex);
}

void KeypadLock::taskThunk(void *pParam) {
  static_cast<KeypadLock *>(pParam)->taskLoop();
}

void KeypadLock::taskLoop() {
  while (true) {
    // keypad_getkey() jest NIEBLOKUJace (zwraca '\0' natychmiast gdy
    // kolejka pusta) - uzycie go tutaj powodowaloby busy-loop i triggerowalo
    // task watchdog (IDLE0 nie dostaje czasu CPU). keypad_waitkey() realnie
    // usypia ten task az do wcisniecia klawisza.
    char key = keypad_waitkey(portMAX_DELAY);
    if (key == '\0') {
      continue;
    }

    if (key == '#') {
      ESP_LOGI(TAG, "Haslo zatwierdzone (%u znakow)",
               static_cast<unsigned>(_bufferLen));
      if (_notifyTask) {
        xTaskNotify(_notifyTask, static_cast<uint32_t>(KeypadEvent::CONFIRMED),
                    eSetBits);
      }
      // Bufor NIE jest tu czyszczony - to AccessController decyduje kiedy
      // wywolac clearBuffer(), po odczytaniu i zweryfikowaniu PINu.
    } else if (key == '*') {
      ESP_LOGI(TAG, "Wprowadzanie anulowane");
      clearBuffer();
      if (_notifyTask) {
        xTaskNotify(_notifyTask, static_cast<uint32_t>(KeypadEvent::CANCELLED),
                    eSetBits);
      }
    } else {
      xSemaphoreTake(_bufferMutex, portMAX_DELAY);
      if (_bufferLen < kMaxPinLen) {
        _buffer[_bufferLen++] = key;
        _buffer[_bufferLen] = '\0';
      } else {
        ESP_LOGW(TAG, "Bufor PIN pelny, znak '%c' odrzucony", key);
      }
      xSemaphoreGive(_bufferMutex);

      if (_notifyTask) {
        xTaskNotify(_notifyTask,
                    static_cast<uint32_t>(KeypadEvent::KEY_PRESSED),
                    eSetBits);
      }
    }
  }
}