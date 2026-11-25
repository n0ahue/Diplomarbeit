#pragma once
#include <Arduino.h>
#include "lcd7.h"

// Resistiver Touch (XPT2046 über SPI).
//
// touchBegin(): Lädt die gespeicherte Kalibrierung. Beim Start erscheint 2 Sekunden lang ein rotes
// Quadrat in der Bildschirmmitte als Notfall-Fallback; regulär kann jederzeit über den Button
// "Kalib." auf dem Display neu kalibriert werden.
void touchBegin(Lcd7& lcd);

// Startet die hochgenaue 5-Punkt-Kalibrierung (4 Ecken + Mitte) und speichert sie im Flash.
void touchCalibrate(Lcd7& lcd);

// true, solange der Bildschirm stabil berührt wird. x/y in Bildschirmkoordinaten (0..799, 0..479).
bool touchGet(int16_t& x, int16_t& y);