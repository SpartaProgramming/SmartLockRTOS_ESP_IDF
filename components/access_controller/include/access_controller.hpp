#pragma once

#include <cstddef>
#include <cstdint>

#include <esp_err.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include "keypad_driver.hpp"
#include "rfid_driver.hpp"

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
  explicit AccessController(KeypadLock &keypad, RfidLock &rfid);
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

  KeypadLock &_keypad;
  RfidLock &_rfid;

  TaskHandle_t _taskHandle;
  SemaphoreHandle_t _stateMutex;
  LockState _state;
};
