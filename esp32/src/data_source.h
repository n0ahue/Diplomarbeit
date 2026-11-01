#pragma once
#include <Arduino.h>

// Liefert die JSON-Antwort der Pi-API in den Puffer "buf".
// Rückgabe: true bei Erfolg.
// Aktuell: Beispieldaten. Später: HTTP-GET per W5500 an den Raspberry Pi.
bool fetchAircraftJson(char* buf, size_t len);
