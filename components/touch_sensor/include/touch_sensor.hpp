#pragma once

#include <cstdint>

#include <driver/gpio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

/**
 * @brief Bity przekazywane przez xTaskNotify(..., eSetBits) do
 * zarejestrowanego taska (poprzez attachNotifyTask). To bity 8-9 - patrz
 * komentarz w keypad_driver.hpp (0-1), rfid_driver.hpp (2-3),
 * contact_sensor.hpp (4-5) i presence_sensor.hpp (6-7). Jesli ten sam
 * task odbiera powiadomienia z wielu zrodel naraz, sprawdzajcie kazdy bit
 * osobno (nie jednym switch), bo eSetBits SUMUJE bitowo (OR), nie
 * nadpisuje.
 */
enum class TouchEvent : uint32_t {
  NONE = 0,
  TOUCHED = 1u << 8,  ///< czujnik dotykowy (np. TTP223B) wykryl dotyk
  RELEASED = 1u << 9, ///< dotyk zostal zwolniony
};

/**
 * @brief Active Object obslugujacy pojedynczy czujnik dotykowy (np.
 * TTP223B). Ten sam wzorzec co ContactSensor/PresenceSensor: ISR na
 * zboczu dowolnym (GPIO_INTR_ANYEDGE) budzi wlasny task, ktory po krotkim
 * debounce aktualizuje stan (chroniony mutexem) i powiadamia
 * zarejestrowany task, jesli stan faktycznie sie zmienil.
 *
 * WAZNE (sprzet): TTP223B (w domyslnym trybie "direct", bez zworki na
 * module) ma wlasny aktywny stopien wyjsciowy (push-pull, HIGH = dotyk) -
 * NIE potrzeba zewnetrznego rezystora podciagajacego, nawet jesli pin
 * nalezy do grupy bez wewnetrznego pull-up (GPIO34-39, np. Board::Touch).
 * Jesli modul ma zworke ustawiona w tryb "toggle" (przelacznik), logika w
 * tej klasie i tak zadziala poprawnie (traktuje kazde zbocze jako zmiane
 * stanu), ale semantyka TOUCHED/RELEASED bedzie odpowiadac wtedy
 * wlaczeniu/wylaczeniu przelacznika, nie fizycznemu trzymaniu palca.
 */
class TouchSensor {
public:
  explicit TouchSensor(gpio_num_t pin);
  ~TouchSensor();

  TouchSensor(const TouchSensor &) = delete;
  TouchSensor &operator=(const TouchSensor &) = delete;
  TouchSensor(TouchSensor &&) = delete;
  TouchSensor &operator=(TouchSensor &&) = delete;

  /// Konfiguruje GPIO/ISR i uruchamia task obslugujacy zdarzenia.
  esp_err_t begin();

  /// Zatrzymuje task, usuwa ISR i zwalnia zasoby.
  void end();

  /// Rejestruje task, ktory ma otrzymywac xTaskNotify przy TOUCHED/RELEASED.
  void attachNotifyTask(TaskHandle_t task);

  /// Aktualny (debounced) stan: true = dotykane. Bezpieczne wywolanie z
  /// dowolnego taska.
  bool isTouched() const;

private:
  static void taskThunk(void *pParam);
  void taskLoop();
  static void IRAM_ATTR isrHandler(void *arg);

  gpio_num_t _pin;
  TaskHandle_t _taskHandle;
  TaskHandle_t _notifyTask;
  QueueHandle_t _isrQueue;
  SemaphoreHandle_t _stateMutex;
  bool _isTouched;
};
