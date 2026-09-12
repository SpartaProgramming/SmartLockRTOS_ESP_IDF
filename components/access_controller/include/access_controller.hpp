#pragma once

#include <cstddef>
#include <cstdint>

#include <esp_err.h>
#include <freertos/FreeRTOS.h>
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

/**
 * @brief Centralny kontroler systemu - jeden task odbiera powiadomienia ze
 * WSZYSTKICH zrodel zdarzen (klawiatura, RFID, kontaktron drzwi, PIR,
 * dotyk, nacisk) przez xTaskNotifyWait (bity 0-11, patrz komentarze w
 * keypad_driver.hpp/rfid_driver.hpp/contact_sensor.hpp/
 * presence_sensor.hpp/touch_sensor.hpp/pressure_sensor.hpp), decyduje o
 * autoryzacji (PIN/UID wzgledem statycznej listy uzytkownikow) i steruje
 * wyjsciami: serwo zamka, wyswietlacz LVGL, diody LED statusu.
 */
class AccessController {
public:
  explicit AccessController(KeypadLock &keypad, RfidLock &rfid,
                            ContactSensor &contact, PresenceSensor &presence,
                            TouchSensor &touch, PressureSensor &pressure,
                            ServoMotor &servo, Display &display,
                            SmartLed &ledGreen, SmartLed &ledRed);
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
  /// Czy aktualne odblokowanie zostalo zainicjowane przez nas (PIN/UID) -
  /// sterownik auto-relocku w handleContactEvent() (patrz tam).
  bool _wasUnlockedByUs;
};
