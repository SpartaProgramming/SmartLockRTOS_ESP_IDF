#pragma once

#include <cstddef>
#include <cstdint>

#include <driver/gpio.h>
#include <driver/spi_master.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

extern "C" {
#include "driver/rc522_spi.h"
#include "picc/rc522_mifare.h"
#include "rc522.h"
#include "rc522_picc.h"
}

enum class RfidEvent : uint32_t {
  NONE = 0,
  CARD_PRESENT =
      1u << 2, ///< karta wykryta i aktywna - UID dostepny przez getUid()
  CARD_REMOVED = 1u << 3, ///< karta zostala odsunieta od czytnika
};

class RfidLock {
public:
  static constexpr size_t kUidHexLen = RC522_PICC_UID_SIZE_MAX * 2;
  static constexpr uint8_t kBlockSize = 16; // RC522_MIFARE_BLOCK_SIZE
  static constexpr uint8_t kDefaultKey[6] = {RC522_MIFARE_KEY_VALUE_DEFAULT};

  explicit RfidLock(spi_host_device_t host, gpio_num_t cs, gpio_num_t rst);
  ~RfidLock();

  RfidLock(const RfidLock &) = delete;
  RfidLock &operator=(const RfidLock &) = delete;
  RfidLock(RfidLock &&) = delete;
  RfidLock &operator=(RfidLock &&) = delete;

  esp_err_t begin();

  void end();

  /// Rejestruje task, ktory ma otrzymywac xTaskNotify przy CARD_PRESENT i
  /// CARD_REMOVED.
  void attachNotifyTask(TaskHandle_t task);

  /// Kopiuje UID ostatnio wykrytej karty jako hex string (zakonczony '\0')
  /// do out. Bezpieczne wywolanie z dowolnego taska. Zwraca dlugosc UID w
  /// bajtach (0, jesli aktualnie nie ma wykrytej karty).
  size_t getUid(char *out, size_t outLen) const;

  /// Czysci zapamietany stan ostatnio wykrytej karty.
  void clearUid();

  /// Odczytuje 16-bajtowy blok danych z ostatnio wykrytej karty.
  /// Adresowanie: blockAddress = sector * 4 + blockInSector. Autoryzacja
  /// KEY_A z podanym kluczem (domyslnie klucz fabryczny - patrz komentarz
  /// TODO(bezpieczenstwo) przy klasie). Zwraca ESP_ERR_INVALID_STATE, gdy
  /// nie ma aktualnie wykrytej karty.
  esp_err_t readBlock(uint8_t sector, uint8_t blockInSector,
                      uint8_t out[kBlockSize],
                      const uint8_t key[6] = kDefaultKey);

  /// Zapisuje 16-bajtowy blok danych na ostatnio wykrytej karcie. Te same
  /// zasady adresowania i autoryzacji co readBlock().
  esp_err_t writeBlock(uint8_t sector, uint8_t blockInSector,
                       const uint8_t data[kBlockSize],
                       const uint8_t key[6] = kDefaultKey);

private:
  static void onPiccStateChangedThunk(void *arg, esp_event_base_t base,
                                      int32_t eventId, void *eventData);
  void onPiccStateChanged(rc522_picc_state_changed_event_t *event);

  spi_host_device_t _host;
  gpio_num_t _cs, _rst;

  rc522_driver_handle_t _driver;
  rc522_handle_t _scanner;
  TaskHandle_t _notifyTask;
  SemaphoreHandle_t _stateMutex;

  rc522_picc_uid_t _lastUid;
  uint8_t _lastSak;
  bool _cardPresent;
};
