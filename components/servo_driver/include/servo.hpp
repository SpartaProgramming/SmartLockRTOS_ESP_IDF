#pragma once

#include <driver/gpio.h>
#include <driver/ledc.h>
#include <esp_err.h>

/**
 * @brief Sterowanie serwem PWM (np. napd zasuwy zamka) przez natywny
 * peryferyjny LEDC ESP-IDF - port starego ServoMotor (Arduino ESP32Servo:
 * attach(pin, 400, 2400) / write(angle)) na ESP-IDF.
 *
 * CELOWO nie ma tu zadnej logiki czasowej/auto-zamykania - ServoMotor to
 * "glupi" aktuator, ktory nie wie nic o czujnikach (np. kontaktronie
 * drzwi). Decyzja "kiedy bezpiecznie zamknac" wymaga wiedzy o fizycznym
 * stanie drzwi, wiec zyje wylacznie w AccessController (patrz tam
 * handleContactEvent()/doorRelockTimeoutCallback() - zamyka WYLACZNIE po
 * potwierdzeniu przez ContactSensor, ze drzwi sa faktycznie zamkniete).
 * Wczesniejsza wersja miala tu wlasny timer auto-zamykajacy (openTimed())
 * - zostala usunieta, bo mogla zatrzasnac zamek przy fizycznie otwartych
 * drzwiach (timer nie mial jak tego sprawdzic).
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
  /// domyslnie 70 - jak w starym DoorLock::closeLatch()). Wywolujacy
  /// (AccessController) jest odpowiedzialny za wywolanie tego WYLACZNIE,
  /// gdy drzwi sa fizycznie zamkniete (patrz komentarz klasy).
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
