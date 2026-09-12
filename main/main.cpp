#include "access_controller.hpp"
#include "contact_sensor.hpp"
#include "display.hpp"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "keypad_driver.hpp"
#include "presence_sensor.hpp"
#include "pressure_sensor.hpp"
#include "rfid_driver.hpp"
#include "servo.hpp"
#include "smart_led.hpp"
#include "touch_sensor.hpp"

#include <driver/spi_master.h>

#include "pin_config.hpp"

static const char *_tag = "SmartLockMain";

static KeypadLock keypad(Board::Keypad::pins.data());
static RfidLock rfid(Board::Spi::host, Board::Rfid::cs, Board::Rfid::rst);
static Display display(Board::Spi::host, Board::LCD::cs, Board::LCD::dc,
                       Board::LCD::rst, Board::LCD::width, Board::LCD::height);
static ContactSensor contactSensor(Board::Contact::pin);
static SmartLed ledGreen(Board::Led::status_green);
static SmartLed ledRed(Board::Led::status_red);
static PresenceSensor presenceSensor(Board::PIR_sensor::pin);
static TouchSensor touchSensor(Board::Touch::touch);
static PressureSensor pressureSensor(Board::AnalogPressure::pin);
static ServoMotor lockServo(Board::SERVO::zamek);
// AccessController zalezy od wszystkich powyzszych obiektow (bierze je
// przez referencje) - stad tworzony na koncu.
static AccessController accessController(keypad, rfid, contactSensor,
                                         presenceSensor, touchSensor,
                                         pressureSensor, lockServo, display,
                                         ledGreen, ledRed);

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

  err = display.begin();
  if (err != ESP_OK) {
    ESP_LOGE(_tag, "Nie udalo sie uruchomic Display: %d", err);
  } else {
    display.showIdle();
  }

  err = contactSensor.begin();
  if (err != ESP_OK) {
    ESP_LOGE(_tag, "Nie udalo sie uruchomic ContactSensor: %d", err);
  }

  ledGreen.blink(500);
  ledRed.blink(500);

  err = presenceSensor.begin();
  if (err != ESP_OK) {
    ESP_LOGE(_tag, "Nie udalo sie uruchomic PresenceSensor: %d", err);
  } else {
    ESP_LOGI(_tag, "Czujnik obecnosci (PIR): %s",
             presenceSensor.isPresent() ? "obecnosc" : "brak");
  }

  err = touchSensor.begin();
  if (err != ESP_OK) {
    ESP_LOGE(_tag, "Nie udalo sie uruchomic TouchSensor: %d", err);
  }

  err = pressureSensor.begin();
  if (err != ESP_OK) {
    ESP_LOGE(_tag, "Nie udalo sie uruchomic PressureSensor: %d", err);
  }

  err = lockServo.begin();
  if (err != ESP_OK) {
    ESP_LOGE(_tag, "Nie udalo sie uruchomic ServoMotor: %d", err);
  }

  err = accessController.begin();
  if (err != ESP_OK) {
    ESP_LOGE(_tag, "Nie udalo sie uruchomic AccessController: %d", err);
  }

  ESP_LOGI(_tag, "System zamka gotowy");
}