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

AccessController::AccessController(KeypadLock &keypad, RfidLock &rfid,
                                   ContactSensor &contact,
                                   PresenceSensor &presence,
                                   TouchSensor &touch,
                                   PressureSensor &pressure, ServoMotor &servo,
                                   Display &display, SmartLed &ledGreen,
                                   SmartLed &ledRed)
    : _keypad(keypad), _rfid(rfid), _contact(contact), _presence(presence),
      _touch(touch), _pressure(pressure), _servo(servo), _display(display),
      _ledGreen(ledGreen), _ledRed(ledRed), _taskHandle(nullptr),
      _stateMutex(nullptr), _state(LockState::LOCKED),
      _wasUnlockedByUs(false) {
  _stateMutex = xSemaphoreCreateMutex();
}

AccessController::~AccessController() {
  end();
  if (_stateMutex) {
    vSemaphoreDelete(_stateMutex);
  }
}

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

  ESP_LOGI(TAG, "AccessController zainicjalizowany");
  return ESP_OK;
}

void AccessController::end() {
  if (_taskHandle) {
    vTaskDelete(_taskHandle);
    _taskHandle = nullptr;
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
  ESP_LOGW(TAG, "Dostep odrzucony");
  _display.showAccessDenied();
  _ledRed.blink(200);
  setState(LockState::LOCKED);
}

void AccessController::handleContactEvent(bool opened) {
  if (opened) {
    ESP_LOGI(TAG, "Drzwi otwarte");
    return;
  }

  ESP_LOGI(TAG, "Drzwi zamkniete");
  // Auto-relock: jesli drzwi otworzono po naszym wlasnym odblokowaniu i
  // teraz sie zamknely, zamykamy zasuwe z powrotem - jak stary
  // handleContactSensor()/handleTiming() w doorLock.cpp.
  if (_wasUnlockedByUs) {
    _servo.close();
    _ledGreen.turnOff();
    _ledRed.turnOn();
    _wasUnlockedByUs = false;
    setState(LockState::LOCKED);
    _display.showIdle();
  }
}

void AccessController::handlePresenceEvent(bool detected) {
  if (!detected) {
    return;
  }
  ESP_LOGI(TAG, "Wykryto ruch (PIR) - wybudzam ekran");
  _display.wakeUp();
  _display.showIdle();
  // TODO(auto-sleep): stary SystemController usypial ekran po czasie
  // bezczynnosci (handleTimeout()) - nie portowane w tym kroku, bo
  // wymagaloby zegara/timeoutu w petli taska zamiast blokujacego na stale
  // xTaskNotifyWait(portMAX_DELAY).
}

void AccessController::handleTouchEvent(bool touched) {
  // TODO(funkcja-dotyku): rola przycisku dotykowego w UX zamka nie jest
  // jeszcze zdefiniowana - na razie tylko logujemy, zeby zdarzenie bylo
  // widoczne i gotowe do podpiecia pod konkretne zachowanie.
  ESP_LOGI(TAG, "Dotyk: %s", touched ? "wykryto" : "zwolniono");
}

void AccessController::handlePressureEvent(bool pressed) {
  // TODO(funkcja-nacisku): rola czujnika nacisku w UX zamka nie jest
  // jeszcze zdefiniowana - na razie tylko logujemy.
  ESP_LOGI(TAG, "Nacisk: %s", pressed ? "wykryto" : "zwolniono");
}
