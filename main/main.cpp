#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "access_controller.hpp"
#include "keypad_driver.hpp"
#include "rfid_driver.hpp"
#include "smart_led.hpp"

#include <driver/spi_master.h>

#include "pin_config.hpp"

static const char *_tag = "SmartLockMain";

static KeypadLock keypad(Board::Keypad::pins.data());
static RfidLock rfid(Board::Spi::host, Board::Rfid::cs, Board::Rfid::rst);
static AccessController accessController(keypad, rfid);

static esp_err_t initSharedSpiBus() {
  spi_bus_config_t busConfig = {};
  busConfig.mosi_io_num = Board::Spi::mosi;
  busConfig.miso_io_num = Board::Spi::miso;
  busConfig.sclk_io_num = Board::Spi::sck;

  busConfig.quadwp_io_num = GPIO_NUM_NC;
  busConfig.quadhd_io_num = GPIO_NUM_NC;

  return spi_bus_initialize(Board::Spi::host, &busConfig, SPI_DMA_DISABLED);
}

extern "C" void app_main(void) {
  ESP_LOGI(_tag, "Uruchamianie systemu Smart Lock...");

  esp_err_t err = initSharedSpiBus();
  if (err != ESP_OK) {
    ESP_LOGE(_tag, "Nie udalo sie zainicjalizowac magistrali SPI: %d", err);
  }

  err = rfid.begin();
  if (err != ESP_OK) {
    ESP_LOGE(_tag, "Nie udalo sie uruchomic RfidLock: %d", err);
  }

  err = keypad.begin();
  if (err != ESP_OK) {
    ESP_LOGE(_tag, "Nie udalo sie uruchomic KeypadLock: %d", err);
  }

  err = accessController.begin();
  if (err != ESP_OK) {
    ESP_LOGE(_tag, "Nie udalo sie uruchomic AccessController: %d", err);
  }

  ESP_LOGI(_tag, "System zamka gotowy");
}