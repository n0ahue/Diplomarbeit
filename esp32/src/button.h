#pragma once
#include <Arduino.h>

enum ButtonEvent { BTN_NONE, BTN_SHORT, BTN_LONG };

void buttonInit();

// Nicht blockierend in loop() aufrufen.
// BTN_SHORT: kurzer Druck, wird beim Loslassen gemeldet.
// BTN_LONG:  langer Druck, wird gemeldet, sobald die Haltezeit erreicht ist (noch beim Halten).
// Pro Tastendruck kommt genau ein Ereignis.
ButtonEvent buttonPoll();
