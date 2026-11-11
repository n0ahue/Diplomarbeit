#pragma once
#include "aircraft.h"

// Alles, was das Display betrifft, liegt in display.cpp.
// Beim Wechsel auf TFT wird nur diese Datei ausgetauscht.
void displayInit();

// dtSec: Zeit seit den letzten Daten (in Flugzeit-Sekunden). Die Positionen werden
// damit entlang Kurs und Geschwindigkeit fortgeschrieben, das ergibt flüssige Bewegung.
void displayDrawRadar(const Aircraft* ac, int count, float dtSec);

void displayDrawDetail(const Aircraft* ac, int count, int selected);
void displayShowError(const char* msg);
