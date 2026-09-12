#pragma once

#include <cstdint>

#include <driver/gpio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

/**
 * @brief Bity przekazywane przez xTaskNotify(..., eSetBits) do
 * zarejestrowanego taska (poprzez attachNotifyTask). To bity 4-5 - patrz
 * komentarz w keypad_driver.hpp (bity 0-1 to KeypadEvent) i
 * rfid_driver.hpp (bity 2-3 to RfidEvent). Jesli ten sam task odbiera
 * powiadomienia z wielu zrodel naraz, sprawdzajcie kazdy bit osobno (nie
 * jednym switch), bo eSetBits SUMUJE bitowo (OR), nie nadpisuje.
 */
enum class ContactEvent : uint32_t {
  NONE = 0,
  OPENED = 1u << 4, ///< drzwi zostaly otwarte (kontaktron rozwarty)
  CLOSED = 1u << 5, ///< drzwi zostaly zamkniete (kontaktron zwarty magnesem)
};

/**
 * @brief Active Object obslugujacy kontaktron (czujnik magnetyczny
 * drzwi/okna) - ISR na zboczu dowolnym (GPIO_INTR_ANYEDGE) budzi wlasny
 * task, ktory debounce'uje odczyt i - jesli stan faktycznie sie zmienil -
 * aktualizuje wewnetrzny stan (chroniony mutexem) oraz powiadamia
 * zarejestrowany task (task notification).
 *
 * WAZNE (sprzet): piny GPIO34-39 (w tym GPIO36/SENSOR_VP) NIE MAJA
 * wewnetrznego rezystora podciagajacego - jesli kontaktron jest podlaczony
 * do jednego z tych pinow, wymagany jest ZEWNETRZNY rezystor podciagajacy
 * (np. 10k do 3.3V), inaczej pin bedzie plywal (floating) przy otwartych
 * drzwiach. begin() probuje ustawic wewnetrzny pull-up i loguje ostrzezenie
 * (ESP_LOGW), jesli sprzet go nie obsluguje - to NIE jest traktowane jako
 * blad krytyczny (begin() i tak zwroci ESP_OK), bo z zewnetrznym
 * rezystorem wszystko dziala poprawnie mimo tego ostrzezenia.
 */
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
