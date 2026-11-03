#pragma once
#include <Arduino.h>

enum ButtonEvent { BTN_NONE, BTN_PRESSED };

void buttonInit();

// Nicht blockierend in loop() aufrufen.
// Liefert BTN_PRESSED genau einmal pro Tastendruck, sofort beim Drücken.
ButtonEvent buttonPoll();
