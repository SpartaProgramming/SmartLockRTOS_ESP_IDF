#pragma once

#include <cstdint>

/**
 * @brief Bity zdarzen sieciowych przekazywane przez xTaskNotify(...,
 * eSetBits) do zarejestrowanego taska (poprzez attachNotifyTask). To bity
 * 13-14 - patrz przydzial bitow w keypad_driver.hpp (0-1, 12),
 * rfid_driver.hpp (2-3), contact_sensor.hpp (4-5), presence_sensor.hpp
 * (6-7), touch_sensor.hpp (8-9), pressure_sensor.hpp (10-11).
 */
namespace AccessEvents {
constexpr uint32_t kWifiConnected = 1u << 13;
constexpr uint32_t kWifiDisconnected = 1u << 14;
} // namespace AccessEvents
