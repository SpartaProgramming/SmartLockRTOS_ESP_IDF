#pragma once

#include <cstdint>

/**
 * @brief Rodzaj wiadomosci telemetrycznej - decyduje, na ktory topic MQTT
 * i w jakim formacie TelemetryService ja opublikuje (patrz
 * telemetry_service.cpp: publishStateSnapshot()/publishEvent()/
 * publishDiagnostics()).
 */
enum class TelemetryMessageType : uint8_t {
  STATE_UPDATE, ///< zmiana stanu zamka (LOCKED/UNLOCKED) - topic .../state
  EVENT_LOG,    ///< pojedyncze zdarzenie (np. dostep przyznany) - .../event
  DIAGNOSTICS,  ///< telemetria "na zadanie" - .../diagnostics
};

/**
 * @brief Uniwersalna, lekka wiadomosc trafiajaca do telemetryQueue.
 * CELOWO plain-POD: bez std::string, bez wskaznikow na sterte - tylko
 * stale bufory znakowe, zeby dalo sie ja bezpiecznie kopiowac PRZEZ
 * WARTOSC miedzy rdzeniem 1 (producenci - AccessController/sensory) a
 * rdzeniem 0 (TelemetryService) przez zwykla kolejke FreeRTOS.
 *
 * Nie wszystkie pola sa uzywane dla kazdego "type" - patrz komentarze przy
 * kazdej grupie pol.
 */
struct TelemetryMessage {
  TelemetryMessageType type;
  int64_t timestampMs; ///< esp_timer_get_time()/1000 w chwili wystawienia

  // --- STATE_UPDATE ---
  bool locked; ///< true = LOCKED, false = UNLOCKED/OPEN

  // --- EVENT_LOG ---
  char eventName[32];   ///< np. "ACCESS_GRANTED", "ACCESS_DENIED", "DOOR_OPENED"
  char eventDetail[64]; ///< np. imie i nazwisko uzytkownika albo UID karty

  // --- DIAGNOSTICS ---
  // Brak dodatkowych pol - TelemetryService sam odczytuje RAM/heap/uptime
  // z systemu w chwili publikacji (patrz publishDiagnostics()). Ten
  // wariant sluzy tylko do wymuszenia publikacji "na zadanie" (poza
  // zwyklym interwalem 300s).
};

/**
 * @brief Rozpoznane komendy przychodzace z MQTT (topic .../command).
 */
enum class SystemCommandId : uint8_t {
  UNLOCK,
  LOCK,
  REBOOT,
  /// Kamera (Raspberry Pi, topic smartlock/<vision_id>/event) rozpoznala
  /// twarz - parameter = imie rozpoznanej osoby (pole "detail" z JSON-a).
  FACE_RECOGNIZED,
  UNKNOWN, ///< nierozpoznana/niepoprawna komenda - nigdy nie trafia do kolejki
};

/**
 * @brief Komenda dla AccessControllera, wystawiana do commandQueue przez
 * TelemetryService po sparsowaniu JSON-a z topicu .../command. Rowniez
 * plain-POD z tych samych powodow co TelemetryMessage.
 */
struct SystemCommand {
  SystemCommandId id;
  char parameter[32]; ///< opcjonalny argument komendy, "" jesli brak
};
