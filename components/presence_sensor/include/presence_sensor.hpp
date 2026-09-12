#pragma once

#include <cstdint>

#include <driver/gpio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

/**
 * @brief Bity przekazywane przez xTaskNotify(..., eSetBits) do
 * zarejestrowanego taska (poprzez attachNotifyTask). To bity 6-7 - patrz
 * komentarz w keypad_driver.hpp (0-1), rfid_driver.hpp (2-3) i
 * contact_sensor.hpp (4-5). Jesli ten sam task odbiera powiadomienia z
 * wielu zrodel naraz, sprawdzajcie kazdy bit osobno (nie jednym switch),
 * bo eSetBits SUMUJE bitowo (OR), nie nadpisuje.
 */
enum class PresenceEvent : uint32_t {
  NONE = 0,
  DETECTED = 1u << 6, ///< czujnik PIR wykryl ruch/obecnosc
  CLEARED = 1u << 7,  ///< sygnal PIR wrocil do stanu spoczynku
};

/**
 * @brief Active Object obslugujacy czujnik PIR (np. HC-SR501) wykrywajacy
 * obecnosc/ruch. Ten sam wzorzec co ContactSensor: ISR na zboczu dowolnym
 * (GPIO_INTR_ANYEDGE) budzi wlasny task, ktory po krotkim debounce
 * aktualizuje stan (chroniony mutexem) i powiadamia zarejestrowany task,
 * jesli stan faktycznie sie zmienil.
 *
 * WAZNE (sprzet): w przeciwienstwie do kontaktronu (bierny styk), moduly
 * PIR typu HC-SR501 maja WLASNY aktywny stopien wyjsciowy (push-pull) -
 * NIE potrzeba zewnetrznego rezystora podciagajacego, nawet jesli pin
 * nalezy do grupy bez wewnetrznego pull-up (GPIO34-39). Upewnij sie
 * jednak, ze poziom wyjsciowy modulu PIR jest kompatybilny z 3.3V (wiekszosc
 * tanich HC-SR501 to zapewnia niezaleznie od napiecia zasilania VCC).
 */
class PresenceSensor {
public:
  explicit PresenceSensor(gpio_num_t pin);
  ~PresenceSensor();

  PresenceSensor(const PresenceSensor &) = delete;
  PresenceSensor &operator=(const PresenceSensor &) = delete;
  PresenceSensor(PresenceSensor &&) = delete;
  PresenceSensor &operator=(PresenceSensor &&) = delete;

  /// Konfiguruje GPIO/ISR i uruchamia task obslugujacy zdarzenia.
  esp_err_t begin();

  /// Zatrzymuje task, usuwa ISR i zwalnia zasoby.
  void end();

  /// Rejestruje task, ktory ma otrzymywac xTaskNotify przy DETECTED/CLEARED.
  void attachNotifyTask(TaskHandle_t task);

  /// Aktualny (debounced) stan: true = wykryto obecnosc/ruch. Bezpieczne
  /// wywolanie z dowolnego taska.
  bool isPresent() const;

private:
  static void taskThunk(void *pParam);
  void taskLoop();
  static void IRAM_ATTR isrHandler(void *arg);

  gpio_num_t _pin;
  TaskHandle_t _taskHandle;
  TaskHandle_t _notifyTask;
  QueueHandle_t _isrQueue;
  SemaphoreHandle_t _stateMutex;
  bool _isPresent;
};
