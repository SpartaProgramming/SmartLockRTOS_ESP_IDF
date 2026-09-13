#pragma once

#include <cstdint>

#include <driver/gpio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

enum class ContactEvent : uint32_t {
  NONE = 0,
  OPENED = 1u << 4, ///< drzwi zostaly otwarte (kontaktron rozwarty)
  CLOSED = 1u << 5, ///< drzwi zostaly zamkniete (kontaktron zwarty magnesem)
};

class ContactSensor {
public:
  explicit ContactSensor(gpio_num_t pin);
  ~ContactSensor();

  ContactSensor(const ContactSensor &) = delete;
  ContactSensor &operator=(const ContactSensor &) = delete;
  ContactSensor(ContactSensor &&) = delete;
  ContactSensor &operator=(ContactSensor &&) = delete;

  /// Konfiguruje GPIO/ISR i uruchamia task obslugujacy zdarzenia.
  esp_err_t begin();

  /// Zatrzymuje task, usuwa ISR i zwalnia zasoby.
  void end();

  /// Rejestruje task, ktory ma otrzymywac xTaskNotify przy OPENED/CLOSED.
  void attachNotifyTask(TaskHandle_t task);

  /// Aktualny (debounced) stan: true = otwarte. Bezpieczne wywolanie z
  /// dowolnego taska.
  bool isOpen() const;

private:
  static void taskThunk(void *pParam);
  void taskLoop();
  static void IRAM_ATTR isrHandler(void *arg);

  gpio_num_t _pin;
  TaskHandle_t _taskHandle;
  TaskHandle_t _notifyTask;
  QueueHandle_t _isrQueue;
  SemaphoreHandle_t _stateMutex;
  bool _isOpen;
};
