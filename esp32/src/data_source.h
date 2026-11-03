#pragma once
#include <Arduino.h>

// Einmal in setup() aufrufen (verbindet bei Bedarf das WLAN).
void dataSourceInit();

// Liefert die JSON-Antwort der Pi-API in den Puffer "buf".
// Rückgabe: true bei Erfolg.
// Aktuell: WLAN-Test per HTTP-GET. Später: W5500-Ethernet statt WLAN.
bool fetchAircraftJson(char* buf, size_t len);
