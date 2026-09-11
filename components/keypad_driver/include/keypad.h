// Copyright 2022  William Henrique (william.martins@ee.ufcg.edu.br)
// Zmodyfikowano: przeniesiono skanowanie kolumn/debounce z ISR do taska,
// dodano extern "C" dla poprawnej konsumpcji z plikow .cpp.
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

/**
 * @file keypad.h
 * @brief 4x4 Keypad driver dla ESP-IDF (wersja poprawiona pod katem ISR-safety)
 */

#ifndef KEYPAD_H
#define KEYPAD_H

#include <driver/gpio.h>
#include <freertos/FreeRTOS.h>

#ifdef __cplusplus
extern "C" {
#endif

#define KEYPAD_DEBOUNCING 30

#define KEYPAD_STACKSIZE 2048

char keypad_waitkey(TickType_t timeout);
/**
 * @brief Inicjalizuje klawiature: kierunki GPIO, ISR na wierszach, kolejki,
 * task skanujacy.
 *
 * @param keypad_pins Tablica pinow w kolejnosci {R1, R2, R3, R4, C1, C2, C3,
 * C4}
 * @return ESP_OK w przypadku powodzenia
 */
esp_err_t keypad_initalize(gpio_num_t keypad_pins[8]);

/**
 * @brief Zwraca ostatnio wciśnięty klawisz.
 * @return Znak klawisza albo '\0' jeśli kolejka jest pusta.
 */
char keypad_getkey(void);

/**
 * @brief Zwalnia zasoby: task skanujacy, ISR, kolejki.
 */
void keypad_delete(void);

#ifdef __cplusplus
}
#endif

#endif // KEYPAD_H