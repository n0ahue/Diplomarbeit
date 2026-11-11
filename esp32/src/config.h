#pragma once
#include <stdint.h>
#include <stddef.h>

// ---------- Zentrale Konfiguration FlightScanr (ESP32) ----------
constexpr float HOME_LAT = 47.8f;   // Platzhalter: Antennenstandort anpassen
constexpr float HOME_LON = 13.0f;   // Platzhalter: Antennenstandort anpassen
constexpr float RANGE_KM = 100.0f;  // Radius des Radars

constexpr int    MAX_AC         = 20;     // Max. Flugzeuge im RAM
constexpr size_t JSON_BUF_SIZE  = 4096;   // Puffer für die JSON-Antwort

// ---------- Datenquelle ----------
// Reihenfolge: Simulation > Beispieldaten > echte Daten vom Pi
constexpr bool  USE_SIMULATION = false;    // true = fünf bewegte Testflugzeuge
constexpr float SIM_SPEEDUP    = 10.0f;   // Zeitraffer der Simulation
constexpr bool  USE_SAMPLE_DATA = false;  // true = feste Beispieldaten
#define PI_HOST "10.42.0.1"               // IP des Raspberry Pi (Hotspot)
constexpr int PI_PORT = 8080;
#define PI_PATH "/api/radar"

// ---------- Aktualisierung und Animation ----------
constexpr uint32_t REFRESH_MS = 1000;     // Daten holen (Pi/Simulation)
constexpr uint32_t FRAME_MS   = 50;       // Radar neu zeichnen (50 ms = 20 Bilder/s)
constexpr float MAX_EXTRAPOLATION_S = 5.0f; // so lange werden Positionen ohne neue Daten fortgeschrieben
constexpr float TIME_SCALE = USE_SIMULATION ? SIM_SPEEDUP : 1.0f;  // Simulation läuft im Zeitraffer

// ---------- Taster ----------
constexpr int      BUTTON_PIN     = 32;   // Taster zwischen GPIO32 und GND
constexpr uint32_t DEBOUNCE_MS    = 50;   // Signal muss so lange stabil sein
constexpr uint32_t MIN_PRESS_GAP_MS = 250; // Sperrzeit nach einem Druck
