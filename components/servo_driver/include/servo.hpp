#pragma once

#include <driver/gpio.h>
#include <driver/ledc.h>
#include <esp_err.h>

/**
 * @brief Sterowanie serwem PWM (np. napd zasuwy zamka) przez natywny
 * peryferyjny LEDC ESP-IDF - port starego ServoMotor (Arduino ESP32Servo:
 * attach(pin, 400, 2400) / write(angle)) na ESP-IDF, bo biblioteka Servo
 * byla zaleznoscia warstwy Arduino, ktorej ten projekt nie uzywa.
 *
 * Oprocz surowego writeAngle() (0-180 stopni, jak w oryginale) udostepnia
 * wyzszy interfejs open()/close() z konfigurowalnymi katami koncowymi -
 * domyslnie 160/70, czyli dokladnie wartosci z oryginalnego
 * DoorLock::openLatch()/closeLatch().
 *
 * UWAGA: klasa na sztywno uzywa LEDC_TIMER_0/LEDC_CHANNEL_0 (zaklada
 * jedno serwo w calym projekcie - napd zamka). Przy wielu serwach
 * naraz trzeba by przydzielac osobne kanaly/timery per instancja.
 */
class ServoMotor {
public:
  /// minPulseUs/maxPulseUs - szerokosc impulsu (w mikrosekundach)
  /// odpowiadajaca katom 0/180 stopni. Domyslnie 500-2400us (typowy zakres
  /// dla wiekszosci serw hobbystycznych, zblizony do oryginalnego 400-2400).
  explicit ServoMotor(gpio_num_t pin, int minPulseUs = 500,
                      int maxPulseUs = 2400);
  ~ServoMotor();

  ServoMotor(const ServoMotor &) = delete;
  ServoMotor &operator=(const ServoMotor &) = delete;
  ServoMotor(ServoMotor &&) = delete;
  ServoMotor &operator=(ServoMotor &&) = delete;

  /// Konfiguruje LEDC (timer + kanal) i ustawia serwo w pozycji zamknietej.
  esp_err_t begin();

  /// Zatrzymuje generowanie PWM (serwo przestaje trzymac pozycje).
  void end();

  /// Jedzie do zadanego kata (0-180 stopni, wartosci spoza zakresu sa
  /// przycinane, tak jak w oryginale).
  void writeAngle(int angle);

  /// Otwiera zamek (jedzie do kata ustawionego przez setOpenAngle(),
  /// domyslnie 160 - jak w starym DoorLock::openLatch()).
  void open();

  /// Zamyka zamek (jedzie do kata ustawionego przez setCloseAngle(),
  /// domyslnie 70 - jak w starym DoorLock::closeLatch()).
  void close();

  void setOpenAngle(int angle);
  void setCloseAngle(int angle);

  /// Ostatnio zadany kat (-1, jesli writeAngle() jeszcze nie bylo wywolane).
  int currentAngle() const;

private:
  void writePulseUs(int pulseUs);

  gpio_num_t _pin;
  int _minPulseUs;
  int _maxPulseUs;
  int _openAngle;
  int _closeAngle;
  int _currentAngle;
  bool _attached;
};
