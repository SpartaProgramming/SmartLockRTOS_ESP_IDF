// Copyright 2022  William Henrique (william.martins@ee.ufcg.edu.br)
// Zmodyfikowano 2026: naprawiono uzycie funkcji nie-ISR-safe wewnatrz
// przerwania (gpio_set_pull_mode/gpio_intr_enable/gpio_intr_disable) oraz
// niedzialajacy debounce oparty o time(NULL) (rozdzielczosc 1s).
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "keypad.h"

#include <esp_log.h>
#include <string.h>

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

static const char *TAG = "keypad";

/** \brief Mapowanie klawiszy matrycy */
static const char keypad_map[16] = {'1', '2', '3', 'A', '4', '5', '6', 'B',
                                    '7', '8', '9', 'C', '*', '0', '#', 'D'};

/** \brief Piny klawiatury: {R1,R2,R3,R4,C1,C2,C3,C4} */
static gpio_num_t _keypad_pins[8];

/** \brief Kolejka "surowych" zdarzen z ISR (tylko numer wiersza) */
static QueueHandle_t row_isr_queue = NULL;

/** \brief Kolejka publiczna z odczytanymi (debounced) klawiszami */
static QueueHandle_t keypad_queue = NULL;

/** \brief Task wykonujacy faktyczny skan kolumn i debounce */
static TaskHandle_t scanner_task_handle = NULL;

static void intr_click_handler(void *args);
static void keypad_scanner_task(void *arg);

/**
 * @brief Wlacza podciagniecie wierszy i ISR - przygotowuje klawiature do
 * oczekiwania na wcisniecie klawisza. Wywolywane WYLACZNIE z kontekstu taska.
 */
static void turnon_rows(void) {
  for (int i = 4; i < 8; i++) /* kolumny */
  {
    gpio_set_pull_mode(_keypad_pins[i], GPIO_PULLDOWN_ONLY);
  }
  for (int i = 0; i < 4; i++) /* wiersze */
  {
    gpio_set_pull_mode(_keypad_pins[i], GPIO_PULLUP_ONLY);
    gpio_intr_enable(_keypad_pins[i]);
  }
}

/**
 * @brief Wylacza ISR na wierszach i przelacza piny do odczytu wcisnietej
 * kolumny.
 */
static void turnon_cols(void) {
  for (int i = 0; i < 4; i++) /* wiersze */
  {
    gpio_intr_disable(_keypad_pins[i]);
    gpio_set_pull_mode(_keypad_pins[i], GPIO_PULLDOWN_ONLY);
  }
  for (int i = 4; i < 8; i++) /* kolumny */
  {
    gpio_set_pull_mode(_keypad_pins[i], GPIO_PULLUP_ONLY);
  }
}

esp_err_t keypad_initalize(gpio_num_t keypad_pins[8]) {
  memcpy(_keypad_pins, keypad_pins, 8 * sizeof(gpio_num_t));

  row_isr_queue = xQueueCreate(8, sizeof(int));
  keypad_queue = xQueueCreate(5, sizeof(char));

  if (row_isr_queue == NULL || keypad_queue == NULL) {
    return ESP_ERR_NO_MEM;
  }

  ESP_ERROR_CHECK_WITHOUT_ABORT(gpio_install_isr_service(ESP_INTR_FLAG_EDGE));

  for (int i = 0; i < 4; i++) /* wiersze */
  {
    gpio_intr_disable(keypad_pins[i]);
    gpio_set_direction(keypad_pins[i], GPIO_MODE_INPUT);
    gpio_set_intr_type(keypad_pins[i], GPIO_INTR_NEGEDGE);
    ESP_ERROR_CHECK_WITHOUT_ABORT(gpio_isr_handler_add(
        _keypad_pins[i], intr_click_handler, (void *)(intptr_t)i));
  }
  for (int i = 4; i < 8; i++) /* kolumny */
  {
    gpio_set_direction(keypad_pins[i], GPIO_MODE_INPUT);
  }

  BaseType_t ok = xTaskCreate(keypad_scanner_task, "keypad_scan",
                              KEYPAD_STACKSIZE, NULL, 6, &scanner_task_handle);
  if (ok != pdPASS) {
    ESP_LOGE(TAG, "Nie udalo sie utworzyc taska skanujacego");
    vQueueDelete(row_isr_queue);
    vQueueDelete(keypad_queue);
    row_isr_queue = NULL;
    keypad_queue = NULL;
    return ESP_ERR_NO_MEM;
  }

  turnon_rows();

  return ESP_OK;
}

static void IRAM_ATTR intr_click_handler(void *args) {
  int index = (int)(intptr_t)args; // numer wiersza, ktory wywolal przerwanie
  BaseType_t higherPriorityTaskWoken = pdFALSE;
  xQueueSendFromISR(row_isr_queue, &index, &higherPriorityTaskWoken);
  if (higherPriorityTaskWoken) {
    portYIELD_FROM_ISR();
  }
}

static void keypad_scanner_task(void *arg) {
  int row;
  for (;;) {
    if (xQueueReceive(row_isr_queue, &row, portMAX_DELAY) != pdTRUE) {
      continue;
    }

    vTaskDelay(pdMS_TO_TICKS(KEYPAD_DEBOUNCING));
    if (gpio_get_level(_keypad_pins[row]) != 0) {
      continue;
    }

    turnon_cols();
    for (int col = 4; col < 8; col++) {
      if (!gpio_get_level(_keypad_pins[col])) {
        char key = keypad_map[row * 4 + col - 4];
        xQueueSend(keypad_queue, &key, 0);
        break;
      }
    }
    turnon_rows();

    /* Czekamy na fizyczne puszczenie klawisza, zeby nie zarejestrowac
     * wielokrotnie tego samego przytrzymanego klawisza. */
    while (gpio_get_level(_keypad_pins[row]) == 0) {
      vTaskDelay(pdMS_TO_TICKS(10));
    }
  }
}

char keypad_getkey(void) {
  char key;
  if (!uxQueueMessagesWaiting(keypad_queue)) {
    return '\0';
  }
  xQueueReceive(keypad_queue, &key, portMAX_DELAY);
  return key;
}

char keypad_waitkey(TickType_t timeout)
{
    char key = '\0';
    xQueueReceive(keypad_queue, &key, timeout);
    return key;
}

void keypad_delete(void) {
  if (scanner_task_handle) {
    vTaskDelete(scanner_task_handle);
    scanner_task_handle = NULL;
  }
  for (int i = 0; i < 8; i++) {
    gpio_isr_handler_remove(_keypad_pins[i]);
    gpio_set_direction(_keypad_pins[i], GPIO_MODE_DISABLE);
  }
  if (row_isr_queue) {
    vQueueDelete(row_isr_queue);
    row_isr_queue = NULL;
  }
  if (keypad_queue) {
    vQueueDelete(keypad_queue);
    keypad_queue = NULL;
  }
}