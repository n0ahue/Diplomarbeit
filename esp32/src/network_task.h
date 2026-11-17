#pragma once
#include <Arduino.h>
#include "aircraft.h"

// Momentaufnahme der Flugzeugdaten, wie sie der Netzwerk-Task zuletzt geliefert hat.
struct AircraftSnapshot {
  Aircraft    ac[MAX_AC];
  int         count      = 0;
  uint32_t    version    = 0;        // wird bei jeder Änderung hochgezählt
  uint32_t    lastDataMs = 0;        // Zeitpunkt der letzten erfolgreichen Daten
  const char* error      = nullptr;  // nullptr = alles in Ordnung
};

// Startet den Netzwerk-Task (WLAN verbinden, Daten holen, parsen) auf Kern 0.
void networkTaskStart();

// Kopiert den aktuellen Stand nach "out", wenn er neuer ist als "seenVersion".
// Rückgabe: true, wenn es etwas Neues gab. Blockiert nur für Mikrosekunden.
bool networkGetSnapshot(AircraftSnapshot& out, uint32_t& seenVersion);
