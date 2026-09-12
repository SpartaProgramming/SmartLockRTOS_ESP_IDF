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

AccessController::AccessController(KeypadLock &keypad, RfidLock &rfid)
    : _keypad(keypad), _rfid(rfid), _taskHandle(nullptr), _stateMutex(nullptr),
      _state(LockState::LOCKED) {
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
  } else {
    ESP_LOGI(TAG, "Dostep przyznany (PIN)");
  }

  setState(LockState::UNLOCKED);
}

void AccessController::denyAccess() {
  ESP_LOGW(TAG, "Dostep odrzucony");
  setState(LockState::LOCKED);
}
