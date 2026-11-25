#pragma once
#include <Arduino.h>
#include "network_task.h"
#include "lcd7.h"

// Oberfläche für das 7"-Display: links das Radar, rechts die Bedienleiste mit Radius-Tasten,
// Flugzeugliste und Detailkarte. Bedient wird per Touch.

void guiBegin(Lcd7& lcd);

// Aufrufen, wenn der Netzwerk-Task neue Daten geliefert hat.
void guiNewData(const AircraftSnapshot& snap);

// Berührung an Bildschirmposition x/y (einmal pro Tippen aufrufen).
void guiTap(int x, int y);

// Einmal pro Durchlauf von loop(): berechnet die aktuellen Positionen und zeichnet neu.
void guiRender();
