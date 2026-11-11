// Wzór pliku z danymi dostępowymi. Należy skopiować go jako secrets.hpp i
// uzupełnić.

#pragma once

#include "access_controller.hpp"

inline constexpr char kWifiSsid[] = "";
inline constexpr char kWifiPassword[] = "";

inline constexpr char kMqttUsername[] = "";
inline constexpr char kMqttPassword[] = "";

// Pusty PIN wyłącza logowanie PIN-em.
inline constexpr char kMasterPin[] = "";

inline constexpr UserInfo kUsers[] = {
    {"Imie", "Nazwisko", "00000000"}, // UID karty w HEX
};
