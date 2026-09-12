#pragma once

#include <array>
#include <driver/gpio.h>
#include <driver/spi_master.h>

namespace Board {

namespace Keypad {
// Tablica pinow: {R1, R2, R3, R4, C1, C2, C3, C4}
inline constexpr std::array<gpio_num_t, 8> pins = {
    GPIO_NUM_14, GPIO_NUM_27, GPIO_NUM_26, GPIO_NUM_25,
    GPIO_NUM_33, GPIO_NUM_32, GPIO_NUM_17, GPIO_NUM_16};
} // namespace Keypad
namespace Led {
inline constexpr gpio_num_t status_green = GPIO_NUM_0;
inline constexpr gpio_num_t status_red = GPIO_NUM_5;
} // namespace Led
namespace Spi {
inline constexpr spi_host_device_t host = SPI2_HOST;
inline constexpr gpio_num_t sck = GPIO_NUM_18;
inline constexpr gpio_num_t mosi = GPIO_NUM_23;
inline constexpr gpio_num_t miso = GPIO_NUM_19;
} // namespace Spi
namespace Rfid {
inline constexpr gpio_num_t cs = GPIO_NUM_21; // SDA/SS/CS modulu MFRC522
inline constexpr gpio_num_t rst = GPIO_NUM_22;
} // namespace Rfid
namespace LCD {
inline constexpr gpio_num_t cs = GPIO_NUM_4;
inline constexpr gpio_num_t dc = GPIO_NUM_2;
inline constexpr gpio_num_t rst = GPIO_NUM_13;

inline constexpr int width = 128;
inline constexpr int height = 160;
} // namespace LCD
namespace SERVO {
inline constexpr gpio_num_t zamek = GPIO_NUM_15;
} // namespace SERVO
namespace Contact {
inline constexpr gpio_num_t pin = GPIO_NUM_36;
} // namespace Contact
namespace Touch {
inline constexpr gpio_num_t touch = GPIO_NUM_39;
} // namespace Touch
namespace PIR_sensor {
inline constexpr gpio_num_t pin = GPIO_NUM_34;
} // namespace PIR_sensor
namespace AnalogPressure {
inline constexpr gpio_num_t pin = GPIO_NUM_35;
} // namespace AnalogPressure
} // namespace Board
