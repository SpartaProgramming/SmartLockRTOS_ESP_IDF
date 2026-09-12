#pragma once

#include <cstdint>

#include <driver/gpio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <esp_adc/adc_cali.h>
#include <esp_adc/adc_oneshot.h>

/**
 * @brief Bity przekazywane przez xTaskNotify(..., eSetBits) do
 * zarejestrowanego taska (poprzez attachNotifyTask). To bity 10-11 - patrz
 * komentarz w keypad_driver.hpp (0-1), rfid_driver.hpp (2-3),
 * contact_sensor.hpp (4-5), presence_sensor.hpp (6-7) i
 * touch_sensor.hpp (8-9).
 */
enum class PressureEvent : uint32_t {
  NONE = 0,
  PRESSED = 1u << 10,  ///< odczyt przekroczyl prog (patrz setThresholdMv/setPressedBelowThreshold)
  RELEASED = 1u << 11, ///< odczyt wrocil za prog w druga strone
};

/**
 * @brief Czujnik nacisku podlaczony analogowo (przez zewnetrzny
 * przetwornik/modul kondycjonujacy sygnal) do wejscia ADC. W
 * przeciwienstwie do czujnikow cyfrowych (ContactSensor/PresenceSensor/
 * TouchSensor) ADC nie generuje przerwan sprzetowych na zboczu - dlatego
 * ta klasa NIE uzywa ISR, tylko wlasny task okresowo probkujacy wejscie
 * (co kPollIntervalMs, patrz .cpp) i usredniajacy kilka probek dla
 * stabilnosci odczytu.
 *
 * Konstruktor przyjmuje gpio_num_t (jak reszta driverow w projekcie) i
 * sam mapuje go na jednostke/kanal ADC przez adc_oneshot_io_to_channel() -
 * pin musi byc jednym z wejsc ADC1 (GPIO32-39 na ESP32).
 *
 * Kalibracja (surowy odczyt -> miliwolty) jest wlaczana automatycznie,
 * jesli dany egzemplarz ESP32 ma odpowiednie dane kalibracyjne w eFuse -
 * w przeciwnym razie klasa loguje ostrzezenie i uzywa przyblizonego
 * przeliczenia (mniej dokladnego, ale nadal uzytecznego).
 *
 * Domyslny prog (kDefaultThresholdMv w .cpp) jest tylko punktem startowym
 * - zaleznie od konkretnego czujnika/przetwornika nacisku (zakres napiecia
 * wyjsciowego rozni sie miedzy modulami), dostosuj go przez
 * setThresholdMv() po sprawdzeniu realnych odczytow z readMillivolts().
 */
class PressureSensor {
public:
  explicit PressureSensor(gpio_num_t pin);
  ~PressureSensor();

  PressureSensor(const PressureSensor &) = delete;
  PressureSensor &operator=(const PressureSensor &) = delete;
  PressureSensor(PressureSensor &&) = delete;
  PressureSensor &operator=(PressureSensor &&) = delete;

  /// Konfiguruje ADC (i kalibracje, jesli dostepna) i uruchamia task
  /// probkujacy.
  esp_err_t begin();

  /// Zatrzymuje task i zwalnia zasoby ADC/kalibracji.
  void end();

  /// Rejestruje task, ktory ma otrzymywac xTaskNotify przy PRESSED/RELEASED.
  void attachNotifyTask(TaskHandle_t task);

  /// Ostatni odczyt, usredniony, w miliwoltach. Bezpieczne wywolanie z
  /// dowolnego taska.
  int readMillivolts() const;

  /// Ostatni surowy odczyt ADC (0-4095 przy domyslnej rozdzielczosci 12-bit).
  int readRaw() const;

  /// Czy ostatni odczyt wskazuje "wcisniety" wg progu i kierunku ustawionego
  /// przez setThresholdMv()/setPressedBelowThreshold().
  bool isPressed() const;

  /// Ustawia prog (w mV) rozrozniajacy PRESSED/RELEASED.
  void setThresholdMv(int thresholdMv);

  /// Niektore czujniki/przetworniki nacisku daja NIZSZE napiecie pod
  /// naciskiem (np. FSR w dzielniku napiecia zalezny od orientacji
  /// wlaczenia) - ustaw true (domyslnie), jesli PRESSED ma oznaczac odczyt
  /// PONIZEJ progu, albo false dla odwrotnej logiki (PRESSED = POWYZEJ
  /// progu).
  void setPressedBelowThreshold(bool pressedBelowThreshold);

private:
  static void taskThunk(void *pParam);
  void taskLoop();
  int sampleAveragedRaw();
  int rawToMv(int raw) const;
  void initCalibration();

  gpio_num_t _pin;
  adc_unit_t _unit;
  adc_channel_t _channel;
  adc_oneshot_unit_handle_t _adcHandle;
  adc_cali_handle_t _caliHandle;
  bool _caliEnabled;

  TaskHandle_t _taskHandle;
  TaskHandle_t _notifyTask;
  SemaphoreHandle_t _stateMutex;

  int _lastRaw;
  int _lastMv;
  bool _isPressed;
  int _thresholdMv;
  bool _pressedBelowThreshold;
};
