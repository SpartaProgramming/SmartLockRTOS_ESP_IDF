#include "access_controller.hpp"

#include <climits>
#include <cstring>

#include <esp_log.h>

static const char *TAG = "AccessController";

static const UserInfo kUsers[] = {
    {"Michal", "Zawada", "4A697280"},
    {"Anna", "Zawada", "11223344"},
    {"Agata", "Sinkiewicz", "96502AF9"},
};
static constexpr size_t kUserCount = sizeof(kUsers) / sizeof(kUsers[0]);

static const char *kMasterPin = "12345";

namespace {
constexpr int kDenyBlinkDurationSec = 5;
constexpr uint32_t kDenyBlinkIntervalMs = 200;
constexpr int kDoorRelockDelaySec = 2;
constexpr int kPresenceSleepTimeoutSec = 20;
constexpr int kTouchHoldThresholdSec = 2;
constexpr int kPinEntryTimeoutSec =
    10; // brak nowego znaku -> czysc i wroc do idle
} // namespace

AccessController::AccessController(KeypadLock &keypad, RfidLock &rfid,
                                   ContactSensor &contact,
                                   PresenceSensor &presence, TouchSensor &touch,
                                   PressureSensor &pressure, ServoMotor &servo,
                                   Display &display, SmartLed &ledGreen,
                                   SmartLed &ledRed)
    : _keypad(keypad), _rfid(rfid), _contact(contact), _presence(presence),
      _touch(touch), _pressure(pressure), _servo(servo), _display(display),
      _ledGreen(ledGreen), _ledRed(ledRed), _taskHandle(nullptr),
      _stateMutex(nullptr), _state(LockState::LOCKED), _wasUnlockedByUs(false),
      _denyBlinkTimer(nullptr), _doorRelockTimer(nullptr),
      _presenceSleepTimer(nullptr), _touchHoldTimer(nullptr),
      _pinEntryTimeoutTimer(nullptr), _pinEntryActive(false) {
  _stateMutex = xSemaphoreCreateMutex();
}

AccessController::~AccessController() {
  end();
  if (_stateMutex) {
    vSemaphoreDelete(_stateMutex);
  }
}

namespace {
esp_err_t createOneShotTimer(esp_timer_cb_t callback, void *arg,
                             const char *name, esp_timer_handle_t *out) {
  esp_timer_create_args_t args = {};
  args.callback = callback;
  args.arg = arg;
  args.name = name;
  return esp_timer_create(&args, out);
}

} // namespace

esp_err_t AccessController::begin() {
  BaseType_t ok = xTaskCreate(taskThunk, "access_controller_task", 4096, this,
                              5, &_taskHandle);
  if (ok != pdPASS) {
    ESP_LOGE(TAG, "Nie udalo sie utworzyc taska AccessController");
    return ESP_ERR_NO_MEM;
  }

  _keypad.attachNotifyTask(_taskHandle);
  _rfid.attachNotifyTask(_taskHandle);
  _contact.attachNotifyTask(_taskHandle);
  _presence.attachNotifyTask(_taskHandle);
  _touch.attachNotifyTask(_taskHandle);
  _pressure.attachNotifyTask(_taskHandle);

  esp_err_t err;
  err = createOneShotTimer(&AccessController::denyBlinkTimeoutCallback, this,
                           "ac_deny_blink", &_denyBlinkTimer);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Blad tworzenia timera denyBlink: %d", err);
  }
  err = createOneShotTimer(&AccessController::doorRelockTimeoutCallback, this,
                           "ac_door_relock", &_doorRelockTimer);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Blad tworzenia timera doorRelock: %d", err);
  }
  err = createOneShotTimer(&AccessController::presenceSleepTimeoutCallback,
                           this, "ac_presence_sleep", &_presenceSleepTimer);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Blad tworzenia timera presenceSleep: %d", err);
  }
  err = createOneShotTimer(&AccessController::touchHoldTimeoutCallback, this,
                           "ac_touch_hold", &_touchHoldTimer);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Blad tworzenia timera touchHold: %d", err);
  }
  err = createOneShotTimer(&AccessController::pinEntryTimeoutCallback, this,
                           "ac_pin_entry", &_pinEntryTimeoutTimer);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Blad tworzenia timera pinEntry: %d", err);
  }

  ESP_LOGI(TAG, "AccessController zainicjalizowany");
  return ESP_OK;
}

void AccessController::end() {
  if (_taskHandle) {
    vTaskDelete(_taskHandle);
    _taskHandle = nullptr;
  }
  if (_denyBlinkTimer) {
    esp_timer_stop(_denyBlinkTimer);
    esp_timer_delete(_denyBlinkTimer);
    _denyBlinkTimer = nullptr;
  }
  if (_doorRelockTimer) {
    esp_timer_stop(_doorRelockTimer);
    esp_timer_delete(_doorRelockTimer);
    _doorRelockTimer = nullptr;
  }
  if (_presenceSleepTimer) {
    esp_timer_stop(_presenceSleepTimer);
    esp_timer_delete(_presenceSleepTimer);
    _presenceSleepTimer = nullptr;
  }
  if (_touchHoldTimer) {
    esp_timer_stop(_touchHoldTimer);
    esp_timer_delete(_touchHoldTimer);
    _touchHoldTimer = nullptr;
  }
  if (_pinEntryTimeoutTimer) {
    esp_timer_stop(_pinEntryTimeoutTimer);
    esp_timer_delete(_pinEntryTimeoutTimer);
    _pinEntryTimeoutTimer = nullptr;
  }
}

LockState AccessController::getState() const {
  xSemaphoreTake(_stateMutex, portMAX_DELAY);
  LockState state = _state;
  xSemaphoreGive(_stateMutex);
  return state;
}

void AccessController::setState(LockState newState) {
  xSemaphoreTake(_stateMutex, portMAX_DELAY);
  _state = newState;
  xSemaphoreGive(_stateMutex);
}

void AccessController::taskThunk(void *pParam) {
  static_cast<AccessController *>(pParam)->taskLoop();
}

void AccessController::taskLoop() {
  while (true) {
    uint32_t notifiedValue = 0;
    if (xTaskNotifyWait(0, ULONG_MAX, &notifiedValue, portMAX_DELAY) !=
        pdTRUE) {
      continue;
    }

    if (notifiedValue & static_cast<uint32_t>(KeypadEvent::KEY_PRESSED)) {
      char pin[KeypadLock::kMaxPinLen + 1];
      size_t len = _keypad.getBuffer(pin, sizeof(pin));
      _pinEntryActive = true;
      _display.showPasswordInput(static_cast<uint8_t>(len));

      if (_pinEntryTimeoutTimer) {
        // Kazdy nowy znak restartuje odliczanie do wyczyszczenia.
        esp_timer_stop(_pinEntryTimeoutTimer);
        esp_timer_start_once(_pinEntryTimeoutTimer,
                             static_cast<uint64_t>(kPinEntryTimeoutSec) *
                                 1000000ULL);
      }
    }

    if (notifiedValue & static_cast<uint32_t>(KeypadEvent::CONFIRMED)) {
      char pin[KeypadLock::kMaxPinLen + 1];
      _keypad.getBuffer(pin, sizeof(pin));
      _keypad.clearBuffer();

      setState(LockState::AUTHENTICATING);
      if (checkPin(pin) == AuthResult::SUCCESS) {
        grantAccess(nullptr);
      } else {
        denyAccess();
      }
    }

    if (notifiedValue & static_cast<uint32_t>(KeypadEvent::CANCELLED)) {
      ESP_LOGI(TAG, "Wprowadzanie PINu anulowane");
      resetManualEntry();
      _display.showPasswordInput(0);
    }

    if (notifiedValue & static_cast<uint32_t>(RfidEvent::CARD_PRESENT)) {
      char uid[RfidLock::kUidHexLen + 1];
      _rfid.getUid(uid, sizeof(uid));

      setState(LockState::AUTHENTICATING);
      const UserInfo *user = nullptr;
      if (checkUid(uid, &user) == AuthResult::SUCCESS) {
        grantAccess(user);
      } else {
        denyAccess();
      }
    }

    if (notifiedValue & static_cast<uint32_t>(RfidEvent::CARD_REMOVED)) {
      ESP_LOGI(TAG, "Karta odsunieta");
    }

    if (notifiedValue & static_cast<uint32_t>(ContactEvent::OPENED)) {
      handleContactEvent(true);
    }
    if (notifiedValue & static_cast<uint32_t>(ContactEvent::CLOSED)) {
      handleContactEvent(false);
    }

    if (notifiedValue & static_cast<uint32_t>(PresenceEvent::DETECTED)) {
      handlePresenceEvent(true);
    }
    if (notifiedValue & static_cast<uint32_t>(PresenceEvent::CLEARED)) {
      handlePresenceEvent(false);
    }

    if (notifiedValue & static_cast<uint32_t>(TouchEvent::TOUCHED)) {
      handleTouchEvent(true);
    }
    if (notifiedValue & static_cast<uint32_t>(TouchEvent::RELEASED)) {
      handleTouchEvent(false);
    }

    if (notifiedValue & static_cast<uint32_t>(PressureEvent::PRESSED)) {
      handlePressureEvent(true);
    }
    if (notifiedValue & static_cast<uint32_t>(PressureEvent::RELEASED)) {
      handlePressureEvent(false);
    }
  }
}

AuthResult AccessController::checkUid(const char *uid,
                                      const UserInfo **outUser) {
  for (size_t i = 0; i < kUserCount; i++) {
    if (strcmp(kUsers[i].uid, uid) == 0) {
      if (outUser) {
        *outUser = &kUsers[i];
      }
      return AuthResult::SUCCESS;
    }
  }
  ESP_LOGW(TAG, "Nieznana karta, UID=%s", uid);
  return AuthResult::FAIL;
}

AuthResult AccessController::checkPin(const char *pin) {
  if (strcmp(pin, kMasterPin) == 0) {
    return AuthResult::SUCCESS;
  }
  ESP_LOGW(TAG, "Bledny PIN");
  return AuthResult::FAIL;
}

void AccessController::grantAccess(const UserInfo *user) {
  resetManualEntry();

  if (user) {
    ESP_LOGI(TAG, "Dostep przyznany: %s %s", user->firstName, user->lastName);
    _display.showUserWelcome(user->firstName, user->lastName);
  } else {
    ESP_LOGI(TAG, "Dostep przyznany (PIN)");
    _display.showAccessGranted();
  }

  _servo.open();
  _ledGreen.turnOn();
  _ledRed.turnOff();
  _wasUnlockedByUs = true;
  setState(LockState::UNLOCKED);
}

void AccessController::denyAccess() {
  resetManualEntry();

  ESP_LOGW(TAG, "Dostep odrzucony - migam %ds, potem czerwona na stale",
           kDenyBlinkDurationSec);
  _display.showAccessDenied();
  _ledRed.blink(kDenyBlinkIntervalMs);

  if (_denyBlinkTimer) {
    esp_timer_stop(_denyBlinkTimer);
    esp_timer_start_once(_denyBlinkTimer,
                         static_cast<uint64_t>(kDenyBlinkDurationSec) *
                             1000000ULL);
  }

  setState(LockState::LOCKED);
}

void AccessController::denyBlinkTimeoutCallback(void *arg) {
  AccessController *self = static_cast<AccessController *>(arg);
  self->_ledRed.turnOn();
}

void AccessController::handleContactEvent(bool opened) {
  if (opened) {
    ESP_LOGI(TAG, "Drzwi otwarte");
    if (_doorRelockTimer) {
      // Drzwi znow otwarte - anuluj oczekujace zamkniecie zasuwy.
      esp_timer_stop(_doorRelockTimer);
    }
    return;
  }

  ESP_LOGI(TAG, "Drzwi zamkniete");
  if (_wasUnlockedByUs && _doorRelockTimer) {
    ESP_LOGI(TAG, "Zwloka %ds przed zamknieciem zasuwy", kDoorRelockDelaySec);
    esp_timer_stop(_doorRelockTimer);
    esp_timer_start_once(_doorRelockTimer,
                         static_cast<uint64_t>(kDoorRelockDelaySec) *
                             1000000ULL);
  }
}

void AccessController::doorRelockTimeoutCallback(void *arg) {
  AccessController *self = static_cast<AccessController *>(arg);
  ESP_LOGI(TAG, "Zamykam zasuwe po zwloce");
  self->_servo.close();
  self->_ledGreen.turnOff();
  self->_ledRed.turnOn();
  self->_wasUnlockedByUs = false;
  self->setState(LockState::LOCKED);
  self->_display.showIdle(true);
}

void AccessController::handlePresenceEvent(bool detected) {
  if (!detected) {
    return;
  }
  ESP_LOGI(TAG, "Wykryto ruch (PIR) - wybudzam ekran");
  _display.wakeUp();
  if (!_pinEntryActive) {
    _display.showIdle(getState() == LockState::LOCKED);
  }

  if (_presenceSleepTimer) {

    esp_timer_stop(_presenceSleepTimer);
    esp_timer_start_once(_presenceSleepTimer,
                         static_cast<uint64_t>(kPresenceSleepTimeoutSec) *
                             1000000ULL);
  }
}

void AccessController::presenceSleepTimeoutCallback(void *arg) {
  AccessController *self = static_cast<AccessController *>(arg);
  ESP_LOGI(TAG, "Brak ruchu przez %ds - usypiam ekran",
           kPresenceSleepTimeoutSec);
  self->_display.sleep();
}

void AccessController::handleTouchEvent(bool touched) {
  if (touched) {
    ESP_LOGI(TAG, "Dotyk wykryto - odliczam %ds do otwarcia",
             kTouchHoldThresholdSec);
    if (_touchHoldTimer) {
      esp_timer_stop(_touchHoldTimer);
      esp_timer_start_once(_touchHoldTimer,
                           static_cast<uint64_t>(kTouchHoldThresholdSec) *
                               1000000ULL);
    }
  } else {
    ESP_LOGI(TAG, "Dotyk zwolniony");
    if (_touchHoldTimer) {
      esp_timer_stop(_touchHoldTimer);
    }
  }
}

void AccessController::touchHoldTimeoutCallback(void *arg) {
  AccessController *self = static_cast<AccessController *>(arg);
  ESP_LOGI(TAG, "Przytrzymanie dotyku >= %ds - otwieram zamek",
           kTouchHoldThresholdSec);
  self->resetManualEntry();
  self->_servo.open();
  self->_ledGreen.turnOn();
  self->_ledRed.turnOff();
  self->_wasUnlockedByUs = true;
  self->setState(LockState::UNLOCKED);
  self->_display.showIdle(false);
}

void AccessController::handlePressureEvent(bool pressed) {

  ESP_LOGI(TAG, "Nacisk: %s", pressed ? "wykryto" : "zwolniono");
}

void AccessController::pinEntryTimeoutCallback(void *arg) {
  AccessController *self = static_cast<AccessController *>(arg);
  ESP_LOGI(TAG, "Brak nowego znaku przez %ds", kPinEntryTimeoutSec);
  self->resetManualEntry();
  self->_display.showIdle(self->getState() == LockState::LOCKED);
}

void AccessController::resetManualEntry() {
  _keypad.clearBuffer();
  _pinEntryActive = false;
  if (_pinEntryTimeoutTimer) {
    esp_timer_stop(_pinEntryTimeoutTimer);
  }
}
