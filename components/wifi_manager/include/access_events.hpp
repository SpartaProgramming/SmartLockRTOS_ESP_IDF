#pragma once

#include <cstdint>


namespace AccessEvents {
constexpr uint32_t kWifiConnected = 1u << 13;
constexpr uint32_t kWifiDisconnected = 1u << 14;
} // namespace AccessEvents
