#pragma once
#include <stdint.h>
#include <stddef.h>

// ---------- Zentrale Konfiguration FlightScanr (ESP32) ----------
constexpr float HOME_LAT = 47.0f;   // Platzhalter: Antennenstandort anpassen
constexpr float HOME_LON = 15.0f;   // Platzhalter: Antennenstandort anpassen
constexpr float RANGE_KM = 100.0f;  // Radius des Radars

constexpr int    MAX_AC         = 20;     // Max. Flugzeuge im RAM
constexpr size_t JSON_BUF_SIZE  = 4096;   // Puffer für die JSON-Antwort
constexpr uint32_t REFRESH_MS   = 5000;   // Aktualisierungsintervall

// ---------- Taster ----------
constexpr int      BUTTON_PIN    = 32;    // Taster zwischen GPIO32 und GND
constexpr uint32_t DEBOUNCE_MS   = 30;    // Entprellzeit
constexpr uint32_t LONG_PRESS_MS = 800;   // ab hier gilt ein Druck als "lang"
