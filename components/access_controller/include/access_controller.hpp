#pragma once

#include <cstddef>
#include <cstdint>

#include <esp_err.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include "contact_sensor.hpp"
#include "display.hpp"
#include "keypad_driver.hpp"
#include "presence_sensor.hpp"
#include "pressure_sensor.hpp"
#include "rfid_driver.hpp"
#include "servo.hpp"
#include "smart_led.hpp"
#include "telemetry_types.hpp"
#include "touch_sensor.hpp"

enum class LockState {
  LOCKED,
  UNLOCKED,
  AUTHENTICATING,
};

enum class AuthResult {
  NONE,
  SUCCESS,
  FAIL,
};

/**
 * @brief Dane uzytkownika uprawnionego do otwarcia zamka.
 */
struct UserInfo {
  const char *firstName;
  const char *lastName;
  const char *uid;
};

class AccessController {
public:
  /// telemetryQueue/commandQueue moga byc nullptr, jesli TelemetryService
  /// nie jest uzywany w danej konfiguracji - wysylka telemetrii i odbior
  /// komend zdalnych sa wtedy po prostu pomijane.
  explicit AccessController(KeypadLock &keypad, RfidLock &rfid,
                            ContactSensor &contact, PresenceSensor &presence,
                            TouchSensor &touch, PressureSensor &pressure,
                            ServoMotor &servo, Display &display,
                            SmartLed &ledGreen, SmartLed &ledRed,
                            QueueHandle_t telemetryQueue = nullptr,
                            QueueHandle_t commandQueue = nullptr);
  ~AccessController();

  AccessController(const AccessController &) = delete;
  AccessController &operator=(const AccessController &) = delete;
  AccessController(AccessController &&) = delete;
  AccessController &operator=(AccessController &&) = delete;

  esp_err_t begin();

  void end();

  LockState getState() const;

private:
  static void taskThunk(void *pParam);
  void taskLoop();

  AuthResult checkUid(const char *uid, const UserInfo **outUser);
  AuthResult checkPin(const char *pin);

  void grantAccess(const UserInfo *user);
  void denyAccess();
  void setState(LockState newState);

  void handleContactEvent(bool opened);
  void handlePresenceEvent(bool detected);
  void handleTouchEvent(bool touched);
  void handlePressureEvent(bool pressed);

  /// Wykonuje komende odebrana zdalnie (MQTT/HA) przez commandQueue.
  /// LOCK respektuje TA SAMA zasade bezpieczenstwa co reszta systemu -
  /// nigdy nie zamyka fizycznie zamka, gdy kontaktron pokazuje otwarte
  /// drzwi (patrz komentarz klasy servo.hpp) - w takim przypadku tylko
  /// oznacza _wasUnlockedByUs=true, zeby istniejacy, bezpieczny mechanizm
  /// (handleContactEvent()/doorRelockTimeoutCallback()) zamknal zamek
  /// automatycznie, gdy drzwi faktycznie sie zamkna.
  void handleRemoteCommand(const SystemCommand &cmd);

  static void denyBlinkTimeoutCallback(void *arg);
  static void doorRelockTimeoutCallback(void *arg);
  static void presenceSleepTimeoutCallback(void *arg);
  static void touchHoldTimeoutCallback(void *arg);
  static void pinEntryTimeoutCallback(void *arg);

  void resetManualEntry();

  /// Wysyla TelemetryMessage(EVENT_LOG) do telemetryQueue (nieblokujaco -
  /// jesli kolejka jest pelna albo telemetryQueue==nullptr, zdarzenie po
  /// prostu przepada, z ostrzezeniem w logu). detail moze byc nullptr.
  void sendTelemetryEvent(const char *eventName, const char *detail);

  /// Wysyla TelemetryMessage(STATE_UPDATE) - wolane z setState() dla
  /// LOCKED/UNLOCKED (AUTHENTICATING pomijamy, bo to stan przejsciowy,
  /// nie snapshot warty publikacji).
  void sendTelemetryState(bool locked);

  KeypadLock &_keypad;
  RfidLock &_rfid;
  ContactSensor &_contact;
  PresenceSensor &_presence;
  TouchSensor &_touch;
  PressureSensor &_pressure;
  ServoMotor &_servo;
  Display &_display;
  SmartLed &_ledGreen;
  SmartLed &_ledRed;

  TaskHandle_t _taskHandle;
  SemaphoreHandle_t _stateMutex;
  LockState _state;

  bool _wasUnlockedByUs;

  esp_timer_handle_t _denyBlinkTimer; // timer do migania czerwona dioda przy
                                      // odrzuceniu dostepu
  esp_timer_handle_t
      _doorRelockTimer; // timer do ponownego zamknięcia zamka po jego otwarciu
  esp_timer_handle_t
      _presenceSleepTimer; // timer do usypiania ekranu po braku ruchu
  esp_timer_handle_t _touchHoldTimer; // timer do obsługi przytrzymania dotyku
  esp_timer_handle_t
      _pinEntryTimeoutTimer; // timer do limitowania czasu wpisywania PIN-u

  // Ustawiany na czas wpisywania PIN-u aby zostać przy tym ekranie.
  bool _pinEntryActive;

  /// OUT - do TelemetryService (rdzen 0). Moze byc nullptr - patrz
  /// konstruktor.
  QueueHandle_t _telemetryQueue;

  /// IN - komendy zdalne (MQTT/HA) z TelemetryService. Odczytywana
  /// nieblokujaco w taskLoop(). Moze byc nullptr - patrz konstruktor.
  QueueHandle_t _commandQueue;
};
