#pragma once
#include <Arduino.h>

enum ButtonEvent { BTN_NONE, BTN_SHORT, BTN_LONG };

void buttonInit();

// Nicht blockierend in loop() aufrufen.
// Liefert das Ereignis beim Loslassen des Tasters (kurz oder lang).
ButtonEvent buttonPoll();
