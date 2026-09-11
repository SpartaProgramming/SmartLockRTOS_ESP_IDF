#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "keypad_driver.hpp"
#include "smart_led.hpp"

#include "pin_config.hpp"

#include <climits>
#include <cstring>

static const char *_tag = "SmartLockMain";

extern "C" void app_main(void) {
  ESP_LOGI(_tag, "Uruchamianie systemu Smart Lock...");

  while (1) {
  }
}