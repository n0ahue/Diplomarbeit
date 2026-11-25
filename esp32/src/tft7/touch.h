#pragma once
#include <Arduino.h>
#include "lcd7.h"

// Resistiver Touch (XPT2046 über SPI).
//
// touchBegin(): Lädt die gespeicherte Kalibrierung. Beim Start erscheint 2 Sekunden lang ein rotes
// Quadrat in der Bildschirmmitte: Wird es in dieser Zeit berührt (oder gibt es noch keine
// gespeicherte Kalibrierung), startet die Kalibrierung mit vier Fadenkreuzen. Das Ergebnis wird
// im Flash gespeichert und muss nicht bei jedem Start wiederholt werden.
void touchBegin(Lcd7& lcd);

// true, solange der Bildschirm berührt wird. x/y in Bildschirmkoordinaten (0..799, 0..479).
bool touchGet(int16_t& x, int16_t& y);
