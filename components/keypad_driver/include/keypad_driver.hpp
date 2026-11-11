#pragma once

#include <cstddef>
#include <cstdint>

#include <driver/gpio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

extern "C" {
#include "keypad.h"
}

enum class KeypadEvent : uint32_t {
  NONE = 0,
  CONFIRMED = 1u << 0, ///< uzytkownik wcisnal '#' - PIN gotowy do odczytu
  CANCELLED = 1u << 1, ///< uzytkownik wcisnal '*' - bufor zostal wyczyszczony
  KEY_PRESSED = 1u << 12,
};

class KeypadLock {
public:
  static constexpr size_t kMaxPinLen = 8;

  explicit KeypadLock(const gpio_num_t pins[8]);
  ~KeypadLock();

  KeypadLock(const KeypadLock &) = delete;
  KeypadLock &operator=(const KeypadLock &) = delete;
  KeypadLock(KeypadLock &&) = delete;
  KeypadLock &operator=(KeypadLock &&) = delete;

  /// Inicjalizuje driver GPIO/ISR i uruchamia task odczytujacy klawisze.
  esp_err_t begin();

  /// Zatrzymuje task i zwalnia zasoby drivera.
  void end();

  /// Rejestruje task, ktory ma otrzymywac xTaskNotify przy '#' i '*'.
  void attachNotifyTask(TaskHandle_t task);

  /// Kopiuje aktualny bufor PIN-u do out (zakonczony '\0'). Bezpieczne
  /// wywolanie z dowolnego taska. Zwraca dlugosc bufora w momencie odczytu.
  size_t getBuffer(char *out, size_t outLen) const;

  /// Czysci bufor PIN-u (np. po weryfikacji albo timeoucie po stronie
  /// AccessControllera).
  void clearBuffer();

private:
  static void taskThunk(void *pParam);
  void taskLoop();

  gpio_num_t _pins[8];
  TaskHandle_t _taskHandle;
  TaskHandle_t _notifyTask;
  SemaphoreHandle_t _bufferMutex;

  char _buffer[kMaxPinLen + 1];
  size_t _bufferLen;
};