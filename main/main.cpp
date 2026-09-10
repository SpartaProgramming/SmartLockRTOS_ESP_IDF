#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "smart_led.hpp"

static const char *_tag = "SmartLockMain";

extern "C" void app_main(void) {
  ESP_LOGI(_tag, "Uruchamianie systemu Smart Lock...");

  // Tworzymy obiekt diody (np. na wbudowanym pinie GPIO 2)
  // Obiekt automatycznie uruchamia swoje zadanie w tle we FreeRTOS!
  SmartLed _statusLed(GPIO_NUM_4);

  while (1) {
    // 1. System czeka / gotowy -> wolne miganie co 1 sekundę
    ESP_LOGI(_tag, "Tryb: Oczekiwanie na kod PIN (miganie co 1000ms)");
    _statusLed.blink(1000);
    vTaskDelay(pdMS_TO_TICKS(5000));

    // 2. Poprawny PIN -> zamek otwiera się, dioda świeci ciągle
    ESP_LOGI(_tag, "Tryb: Zamek otwarty (dioda ON)");
    _statusLed.turnOn();
    vTaskDelay(pdMS_TO_TICKS(3000));

    // 3. Błąd lub alarm -> szybkie miganie alarmowe co 200 ms
    ESP_LOGI(_tag, "Tryb: ALARM / Błąd (szybkie miganie co 200ms)");
    _statusLed.blink(200);
    vTaskDelay(pdMS_TO_TICKS(4000));

    // 4. Zamek zablokowany -> wyłączenie diody
    ESP_LOGI(_tag, "Tryb: Zamek zablokowany (dioda OFF)");
    _statusLed.turnOff();
    vTaskDelay(pdMS_TO_TICKS(3000));
  }
}