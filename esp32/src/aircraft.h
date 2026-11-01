#pragma once
#include <Arduino.h>
#include "config.h"

struct Aircraft {
  char  hex[7];
  char  call[9];
  int   alt;      // ft
  int   spd;      // kn
  float hdg;      // Grad
  float lat, lon;
};

// Parst das JSON der Pi-API in das Array "out".
// Rückgabe: Anzahl der Flugzeuge, oder -1 bei einem JSON-Fehler.
int parseAircraft(const char* json, Aircraft* out, int maxCount);
