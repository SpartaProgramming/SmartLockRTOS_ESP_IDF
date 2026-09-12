#pragma once

#include <array>
#include <driver/gpio.h>
#include <driver/spi_master.h>

namespace Board {

namespace Keypad {
// Tablica pinow: {R1, R2, R3, R4, C1, C2, C3, C4}
// C3/C4 przeniesione z GPIO_NUM_18/19 (kolizja z Board::Spi::sck/miso).
inline constexpr std::array<gpio_num_t, 8> pins = {
    GPIO_NUM_14, GPIO_NUM_27, GPIO_NUM_26, GPIO_NUM_25,
    GPIO_NUM_33, GPIO_NUM_32, GPIO_NUM_17, GPIO_NUM_16};
} // namespace Keypad
namespace Led {
inline constexpr gpio_num_t status = GPIO_NUM_4;
}
namespace Spi {
inline constexpr spi_host_device_t host = SPI2_HOST;
inline constexpr gpio_num_t sck = GPIO_NUM_18;
inline constexpr gpio_num_t mosi = GPIO_NUM_23;
inline constexpr gpio_num_t miso = GPIO_NUM_19;
} // namespace Spi
namespace Rfid {
// Piny specyficzne dla urzadzenia MFRC522 na wspolnej magistrali Board::Spi.
inline constexpr gpio_num_t cs = GPIO_NUM_21;  // SDA/SS/CS modulu MFRC522
inline constexpr gpio_num_t rst = GPIO_NUM_22; // sprzetowy reset modulu
} // namespace Rfid

} // namespace Board
