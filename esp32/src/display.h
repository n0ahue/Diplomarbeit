#pragma once
#include "aircraft.h"

// Alles, was das Display betrifft, liegt in display.cpp.
// Beim Wechsel auf TFT wird nur diese Datei ausgetauscht.
void displayInit();

// dtSec:   Zeit seit den letzten Daten in Flugzeit-Sekunden (für das Fortschreiben der Positionen)
// rangeKm: Radius des Radars (Zoomstufe)
// ageSec:  Alter der letzten Daten in echten Sekunden (für die Statusanzeige)
void displayDrawRadar(const Aircraft* ac, int count, float dtSec, float rangeKm, float ageSec);

void displayDrawDetail(const Aircraft* ac, int count, int selected);
void displayShowError(const char* msg);
