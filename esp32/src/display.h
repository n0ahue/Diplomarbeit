#pragma once
#include "aircraft.h"

// Alles, was das Display betrifft, liegt in display.cpp.
// Beim Wechsel auf TFT wird nur diese Datei ausgetauscht.
void displayInit();
void displayDrawRadar(const Aircraft* ac, int count);
void displayShowError(const char* msg);
