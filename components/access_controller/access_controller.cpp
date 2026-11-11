#include "access_controller.hpp"
#include <climits>
#include <cstdio>
#include <cstring>
#include <strings.h>

#include <esp_log.h>
#include <esp_system.h>

#include "secrets.hpp" // kUsers, kMasterPin, kFaceAuthorizedPerson
#include "telemetry_types.hpp"

static const char *TAG = "AccessController";

static const char *kFaceAuthorizedPerson =
    "Michal"; // tylko ja moge wejsc do systemu po rozpoznaniu twarzy

static constexpr size_t kUserCount =
    sizeof(kUsers) / sizeof(kUsers[0]); // ile wpisów w tablicy kUsers

namespace {
constexpr int kDenyBlinkDurationSec =
    5; // czas migania czerwonej diody po odrzuceniu dostepu
constexpr uint32_t kDenyBlinkIntervalMs =
    200; // jak czesto miga czerwona dioda przy odrzuceniu dostepu
constexpr int kDoorRelockDelaySec =
    2; // czas po fizycznym zamknięciu drzwi, po którym zasuwa zostaje zamknięta
       // automatycznie
constexpr int kUnlockTimeoutSec =
    10; // zasuwa zwolniona, ale drzwi nie otwarte -> zamknij ponownie
constexpr bool kDisplaySleepEnabled =
    false; // false = ekran nigdy nie zasypia po braku ruchu
constexpr int kPresenceSleepTimeoutSec = 20;
constexpr int kTouchHoldThresholdSec = 2;
constexpr int kPinEntryTimeoutSec =
    10; // przerwa w pisywaniu PIN - reset bufora i powrot do ekranu
        // spoczynkowego
constexpr uint32_t kCommandPollIntervalMs =
    200; // jak czesto sprawdzamy commandQueue miedzy powiadomieniami
constexpr int kRebootDelayMs =
    1000; // czas na wyslanie telemetrii przed restartem
constexpr int kFaceWindowSec =
    20; // po rozpoznaniu twarzy tyle czasu na PIN/karte
constexpr int kFaceErrorPopupSec = 4;
} // namespace

AccessController::AccessController(
    KeypadLock &keypad, RfidLock &rfid, ContactSensor &contact,
    PresenceSensor &presence, TouchSensor &touch, PressureSensor &pressure,
    ServoMotor &servo, Display &display, SmartLed &ledGreen, SmartLed &ledRed,
    QueueHandle_t telemetryQueue, QueueHandle_t commandQueue)
    : _keypad(keypad), _rfid(rfid), _contact(contact), _presence(presence),
      _touch(touch), _pressure(pressure), _servo(servo), _display(display),
      _ledGreen(ledGreen), _ledRed(ledRed), _taskHandle(nullptr),
      _stateMutex(nullptr), _state(LockState::LOCKED), _wasUnlockedByUs(false),
      _denyBlinkTimer(nullptr), _doorRelockTimer(nullptr),
      _presenceSleepTimer(nullptr), _touchHoldTimer(nullptr),
      _pinEntryTimeoutTimer(nullptr), _unlockTimeoutTimer(nullptr),
      _faceWindowTimer(nullptr), _faceErrorPopupTimer(nullptr),
      _pinEntryActive(false), _faceWindowActive(false),
      _telemetryQueue(telemetryQueue), _commandQueue(commandQueue) {
  _stateMutex = xSemaphoreCreateMutex();
}

AccessController::~AccessController() {
  end();
  if (_stateMutex) {
    vSemaphoreDelete(_stateMutex);
  }
}

// funkcja tworzy timery jednorazowe wywołujące callback
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
  // Rdzen 1 (APP_CPU) - rdzen 0 zarezerwowany dla WiFi.
  BaseType_t ok = xTaskCreatePinnedToCore(taskThunk, "access_controller_task",
                                          4096, this, 5, &_taskHandle, 1);
  if (ok != pdPASS) {
    ESP_LOGE(TAG, "Nie udalo sie utworzyc taska AccessController");
    return ESP_ERR_NO_MEM;
  }

  // Rejestracja w komponantach uchwytu AccessController do powiadamiania go o
  // zdarzeniach
  _keypad.attachNotifyTask(_taskHandle);
  _rfid.attachNotifyTask(_taskHandle);
  _contact.attachNotifyTask(_taskHandle);
  _presence.attachNotifyTask(_taskHandle);
  _touch.attachNotifyTask(_taskHandle);
  _pressure.attachNotifyTask(_taskHandle);

  esp_err_t err;
  err = createOneShotTimer(&AccessController::denyBlinkTimeoutCallback,
                           this, // po odliczeniu czasu, wywołaj autoamtycznie
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
  err = createOneShotTimer(&AccessController::unlockTimeoutCallback, this,
                           "ac_unlock_timeout", &_unlockTimeoutTimer);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Blad tworzenia timera unlockTimeout: %d", err);
  }
  err = createOneShotTimer(&AccessController::faceWindowTimeoutCallback, this,
                           "ac_face_window", &_faceWindowTimer);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Blad tworzenia timera faceWindow: %d", err);
  }
  err = createOneShotTimer(&AccessController::faceErrorPopupTimeoutCallback,
                           this, "ac_face_popup", &_faceErrorPopupTimer);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Blad tworzenia timera faceErrorPopup: %d", err);
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
  if (_unlockTimeoutTimer) {
    esp_timer_stop(_unlockTimeoutTimer);
    esp_timer_delete(_unlockTimeoutTimer);
    _unlockTimeoutTimer = nullptr;
  }
  if (_faceWindowTimer) {
    esp_timer_stop(_faceWindowTimer);
    esp_timer_delete(_faceWindowTimer);
    _faceWindowTimer = nullptr;
  }
  if (_faceErrorPopupTimer) {
    esp_timer_stop(_faceErrorPopupTimer);
    esp_timer_delete(_faceErrorPopupTimer);
    _faceErrorPopupTimer = nullptr;
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

  // Telemetria stanu tylko dla LOCKED/UNLOCKED - AUTHENTICATING to stan
  // przejsciowy, nie snapshot warty publikacji (patrz komentarz w .hpp).
  if (newState == LockState::LOCKED) {
    sendTelemetryState(true);
  } else if (newState == LockState::UNLOCKED) {
    sendTelemetryState(false);
  }
}

void AccessController::taskThunk(void *pParam) {
  static_cast<AccessController *>(pParam)->taskLoop();
}

void AccessController::taskLoop() {
  while (true) {
    uint32_t notifiedValue = 0;
    // Timeout ograniczony (nie portMAX_DELAY) - dzieki temu petla co
    // kCommandPollIntervalMs wraca i sprawdza commandQueue (komendy
    // zdalne z MQTT/HA), nawet gdy zaden czujnik nic nie zglasza.
    BaseType_t notified = xTaskNotifyWait(
        0, ULONG_MAX, &notifiedValue, pdMS_TO_TICKS(kCommandPollIntervalMs));

    if (notified != pdTRUE) {
      notifiedValue = 0;
    }

    // PIN i karta dzialaja tylko w oknie wejscia (po rozpoznaniu twarzy).
    if ((notifiedValue & static_cast<uint32_t>(KeypadEvent::KEY_PRESSED)) &&
        !_faceWindowActive) {
      rejectNoFace();
    } else if (notifiedValue &
               static_cast<uint32_t>(KeypadEvent::KEY_PRESSED)) {
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

    if ((notifiedValue & static_cast<uint32_t>(KeypadEvent::CONFIRMED)) &&
        !_faceWindowActive) {
      rejectNoFace();
    } else if (notifiedValue & static_cast<uint32_t>(KeypadEvent::CONFIRMED)) {
      char pin[KeypadLock::kMaxPinLen + 1];
      _keypad.getBuffer(pin, sizeof(pin));
      _keypad.clearBuffer();

      setState(LockState::AUTHENTICATING);
      if (checkPin(pin) == AuthResult::SUCCESS) {
        grantAccess(nullptr);
      } else {
        denyAccess("PIN");
      }
    }

    if (notifiedValue & static_cast<uint32_t>(KeypadEvent::CANCELLED)) {
      ESP_LOGI(TAG, "Wprowadzanie PINu anulowane");
      resetManualEntry();
      if (_faceWindowActive) {
        _display.showPasswordInput(0);
      }
    }

    if ((notifiedValue & static_cast<uint32_t>(RfidEvent::CARD_PRESENT)) &&
        !_faceWindowActive) {
      rejectNoFace();
    } else if (notifiedValue & static_cast<uint32_t>(RfidEvent::CARD_PRESENT)) {
      char uid[RfidLock::kUidHexLen + 1];
      _rfid.getUid(uid, sizeof(uid));

      setState(LockState::AUTHENTICATING);
      const UserInfo *user = nullptr;
      if (checkUid(uid, &user) == AuthResult::SUCCESS) {
        grantAccess(user);
      } else {
        denyAccess(uid);
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

    // Komendy zdalne (MQTT/HA) - nieblokujaco, niezaleznie od tego, czy
    // powyzej przyszlo jakies powiadomienie z czujnikow.
    if (_commandQueue) {
      SystemCommand cmd;
      if (xQueueReceive(_commandQueue, &cmd, 0) == pdTRUE) {
        handleRemoteCommand(cmd);
      }
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
  closeFaceWindow(); // okno jednorazowe - kolejne wejscie wymaga nowej twarzy

  if (user) {
    ESP_LOGI(TAG, "Dostep przyznany: %s %s", user->firstName, user->lastName);
    _display.showUserWelcome(user->firstName, user->lastName);

    char detail[64];
    snprintf(detail, sizeof(detail), "%s %s", user->firstName, user->lastName);
    sendTelemetryEvent("ACCESS_GRANTED", detail);
  } else {
    ESP_LOGI(TAG, "Dostep przyznany (PIN)");
    _display.showAccessGranted();
    sendTelemetryEvent("ACCESS_GRANTED", "PIN");
  }

  _servo.open();
  _ledGreen.turnOn();
  _ledRed.turnOff();
  _wasUnlockedByUs = true;
  setState(LockState::UNLOCKED);
  startUnlockTimeout();
}

void AccessController::denyAccess(const char *detail) {
  resetManualEntry();

  _display.showAccessDenied();
  _ledRed.blink(kDenyBlinkIntervalMs);
  sendTelemetryEvent("ACCESS_DENIED", detail);

  if (_denyBlinkTimer) {
    esp_timer_stop(_denyBlinkTimer);
    esp_timer_start_once(_denyBlinkTimer,
                         static_cast<uint64_t>(kDenyBlinkDurationSec) *
                             1000000ULL);
  }

  setState(LockState::LOCKED);
}

void AccessController::denyBlinkTimeoutCallback(void *arg) {
  AccessController *self = static_cast<AccessController *>(
      arg); // metoda statyczna nie jest związana z konkretną instancją klasy,
            // więc przekazujemy wskaźnik na obiekt
  self->_ledRed.turnOn();
}

void AccessController::handleContactEvent(bool opened) {
  if (opened) {
    if (_wasUnlockedByUs) {
      ESP_LOGI(TAG, "Drzwi otwarte");
      sendTelemetryEvent("DOOR_OPENED", "AUTHORIZED");
    } else {
      ESP_LOGW(TAG, "Drzwi otwarte bez zwolnienia zasuwy!");
      sendTelemetryEvent("DOOR_OPENED", "FORCED");
    }
    if (_unlockTimeoutTimer) {
      esp_timer_stop(_unlockTimeoutTimer);
    }
    if (_doorRelockTimer) {
      esp_timer_stop(_doorRelockTimer);
    }
    return;
  }

  ESP_LOGI(TAG, "Drzwi zamkniete");
  sendTelemetryEvent("DOOR_CLOSED", "");
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
  self->sendTelemetryEvent("LOCKED", "AUTO");
}

void AccessController::handlePresenceEvent(bool detected) {
  if (!detected) {
    return;
  }
  ESP_LOGI(TAG, "Wykryto ruch (PIR) - wybudzam ekran");
  sendTelemetryEvent("MOTION_DETECTED", "");
  wakeDisplay();
  if (!_pinEntryActive) {
    showRestingScreen();
  }
}

void AccessController::wakeDisplay() {
  _display.wakeUp();
  if (kDisplaySleepEnabled && _presenceSleepTimer) {
    esp_timer_stop(_presenceSleepTimer);
    esp_timer_start_once(_presenceSleepTimer,
                         static_cast<uint64_t>(kPresenceSleepTimeoutSec) *
                             1000000ULL);
  }
}

void AccessController::showRestingScreen() {
  bool locked = getState() == LockState::LOCKED;
  if (_faceWindowActive && locked) {
    _display.showFaceWindow();
  } else {
    _display.showIdle(locked);
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
  self->sendTelemetryEvent("ACCESS_GRANTED", "TOUCH");
  self->startUnlockTimeout();
}

void AccessController::handlePressureEvent(bool pressed) {
  ESP_LOGI(TAG, "Nacisk: %s", pressed ? "wykryto" : "zwolniono");
  if (!pressed) {
    return;
  }

  char detail[16];
  snprintf(detail, sizeof(detail), "%d mV", _pressure.readMillivolts());
  sendTelemetryEvent("PRESSURE_DETECTED", detail);
}

void AccessController::pinEntryTimeoutCallback(void *arg) {
  AccessController *self = static_cast<AccessController *>(arg);
  ESP_LOGI(TAG, "Brak nowego znaku przez %ds", kPinEntryTimeoutSec);
  self->resetManualEntry();
  self->showRestingScreen();
}

void AccessController::startUnlockTimeout() {
  if (_unlockTimeoutTimer) {
    esp_timer_stop(_unlockTimeoutTimer);
    esp_timer_start_once(_unlockTimeoutTimer,
                         static_cast<uint64_t>(kUnlockTimeoutSec) * 1000000ULL);
  }
}

void AccessController::unlockTimeoutCallback(void *arg) {
  AccessController *self = static_cast<AccessController *>(arg);
  if (!self->_wasUnlockedByUs || self->_contact.isOpen()) {
    // Juz zamkniete innym torem albo drzwi otwarte - nie ruszamy zasuwy.
    return;
  }
  ESP_LOGI(TAG, "Drzwi nie otwarte przez %ds - zamykam zasuwe",
           kUnlockTimeoutSec);
  self->_servo.close();
  self->_ledGreen.turnOff();
  self->_ledRed.turnOn();
  self->_wasUnlockedByUs = false;
  self->setState(LockState::LOCKED);
  self->_display.showIdle(true);
  self->sendTelemetryEvent("LOCKED", "TIMEOUT");
}

void AccessController::resetManualEntry() {
  _keypad.clearBuffer();
  _pinEntryActive = false;
  if (_pinEntryTimeoutTimer) {
    esp_timer_stop(_pinEntryTimeoutTimer);
  }
}

void AccessController::sendTelemetryEvent(const char *eventName,
                                          const char *detail) {
  if (!_telemetryQueue) {
    return;
  }

  TelemetryMessage msg = {};
  msg.type = TelemetryMessageType::EVENT_LOG;
  msg.timestampMs = esp_timer_get_time() / 1000;
  strncpy(msg.eventName, eventName, sizeof(msg.eventName) - 1);
  strncpy(msg.eventDetail, detail ? detail : "", sizeof(msg.eventDetail) - 1);

  if (xQueueSend(_telemetryQueue, &msg, 0) != pdTRUE) {
    ESP_LOGW(TAG, "telemetryQueue pelna - zdarzenie '%s' pominiete", eventName);
  }
}

void AccessController::handleRemoteCommand(const SystemCommand &cmd) {
  switch (cmd.id) {
  case SystemCommandId::UNLOCK: {
    ESP_LOGI(TAG, "Komenda zdalna: UNLOCK");
    resetManualEntry();
    _display.showAccessGranted();
    _servo.open();
    _ledGreen.turnOn();
    _ledRed.turnOff();
    _wasUnlockedByUs = true;
    setState(LockState::UNLOCKED);
    sendTelemetryEvent("ACCESS_GRANTED", "REMOTE");
    startUnlockTimeout();
    break;
  }

  case SystemCommandId::LOCK: {
    ESP_LOGI(TAG, "Komenda zdalna: LOCK");
    if (_contact.isOpen()) {
      ESP_LOGW(TAG, "LOCK odroczone - drzwi otwarte, zamknie sie "
                    "automatycznie po ich zamknieciu");
      _wasUnlockedByUs = true;
      sendTelemetryEvent("LOCK_DEFERRED", "door open");
    } else {
      _servo.close();
      _ledGreen.turnOff();
      _ledRed.turnOn();
      _wasUnlockedByUs = false;
      setState(LockState::LOCKED);
      _display.showIdle(true);
      sendTelemetryEvent("LOCKED", "REMOTE");
    }
    break;
  }

  case SystemCommandId::REBOOT: {
    ESP_LOGW(TAG, "Komenda zdalna: REBOOT - restart za %dms", kRebootDelayMs);
    sendTelemetryEvent("REBOOT_REQUESTED", cmd.parameter);
    vTaskDelay(pdMS_TO_TICKS(kRebootDelayMs));
    esp_restart();
    break;
  }

  case SystemCommandId::FACE_RECOGNIZED:
    handleFaceRecognized(cmd.parameter);
    break;

  case SystemCommandId::UNKNOWN:
  default:
    ESP_LOGW(TAG, "Nieznana komenda zdalna - ignoruje");
    break;
  }
}

void AccessController::handleFaceRecognized(const char *person) {
  if (strcasecmp(person, kFaceAuthorizedPerson) != 0) {
    ESP_LOGW(TAG, "Kamera rozpoznala '%s' - brak uprawnien, ignoruje", person);
    return;
  }

  ESP_LOGI(TAG, "Rozpoznano twarz: %s - okno wejscia na %ds", person,
           kFaceWindowSec);
  _faceWindowActive = true;
  if (_faceWindowTimer) {
    // Kolejne rozpoznanie w trakcie okna przedluza je od nowa.
    esp_timer_stop(_faceWindowTimer);
    esp_timer_start_once(_faceWindowTimer,
                         static_cast<uint64_t>(kFaceWindowSec) * 1000000ULL);
  }
  if (_faceErrorPopupTimer) {
    esp_timer_stop(_faceErrorPopupTimer);
  }

  wakeDisplay();
  _display.hidePopup();
  _display.setFaceIndicator(true); // włącza flagę "F"
  if (!_pinEntryActive) {
    showRestingScreen();
  }
  sendTelemetryEvent("FACE_WINDOW_OPEN", person);
}

void AccessController::closeFaceWindow() {
  _faceWindowActive = false;
  if (_faceWindowTimer) {
    esp_timer_stop(_faceWindowTimer);
  }
  _display.setFaceIndicator(false);
}

void AccessController::faceWindowTimeoutCallback(void *arg) {
  AccessController *self = static_cast<AccessController *>(arg);
  ESP_LOGI(TAG, "Okno wejscia (%ds) minelo - czekam na kolejne rozpoznanie",
           kFaceWindowSec);
  self->closeFaceWindow();
  if (!self->_pinEntryActive) {
    self->showRestingScreen();
  }
}

void AccessController::rejectNoFace() {
  resetManualEntry();
  wakeDisplay();
  showRestingScreen();
  _display.showPopup("Face not\nrecognized!");

  bool alreadyShown =
      _faceErrorPopupTimer && esp_timer_is_active(_faceErrorPopupTimer);
  if (!alreadyShown) {
    ESP_LOGW(TAG, "Proba odblokowania bez rozpoznanej twarzy");
    sendTelemetryEvent("ACCESS_DENIED", "NO_FACE");
  }

  if (_faceErrorPopupTimer) {
    esp_timer_stop(_faceErrorPopupTimer);
    esp_timer_start_once(_faceErrorPopupTimer,
                         static_cast<uint64_t>(kFaceErrorPopupSec) *
                             1000000ULL);
  }
}

void AccessController::faceErrorPopupTimeoutCallback(void *arg) {
  static_cast<AccessController *>(arg)->_display.hidePopup();
}

void AccessController::sendTelemetryState(bool locked) {
  if (!_telemetryQueue) {
    return;
  }

  TelemetryMessage msg = {};
  msg.type = TelemetryMessageType::STATE_UPDATE;
  msg.timestampMs = esp_timer_get_time() / 1000;
  msg.locked = locked;

  if (xQueueSend(_telemetryQueue, &msg, 0) != pdTRUE) {
    ESP_LOGW(TAG, "telemetryQueue pelna - aktualizacja stanu pominieta");
  }
}
