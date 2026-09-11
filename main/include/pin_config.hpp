#pragma once

#include <array>
#include <driver/gpio.h>

namespace Board {

namespace Keypad {
// Tablica pinow: {R1, R2, R3, R4, C1, C2, C3, C4}
inline constexpr std::array<gpio_num_t, 8> pins = {
    GPIO_NUM_14, GPIO_NUM_27, GPIO_NUM_26, GPIO_NUM_25,
    GPIO_NUM_33, GPIO_NUM_32, GPIO_NUM_18, GPIO_NUM_19};
} // namespace Keypad
namespace Led {
inline constexpr gpio_num_t status = GPIO_NUM_4;
}

} // namespace Board
