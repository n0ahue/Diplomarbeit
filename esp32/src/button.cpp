#include "button.h"
#include "config.h"

void buttonInit() {
  pinMode(BUTTON_PIN, INPUT_PULLUP);  // Taster schaltet gegen GND
}

ButtonEvent buttonPoll() {
  static bool     lastReading = false;
  static bool     stable      = false;
  static uint32_t lastChange  = 0;
  static uint32_t pressStart  = 0;

  const bool     down = (digitalRead(BUTTON_PIN) == LOW);
  const uint32_t now  = millis();

  if (down != lastReading) {   // Zustand hat gewechselt: Entprellzeit neu starten
    lastReading = down;
    lastChange  = now;
  }

  if ((now - lastChange) >= DEBOUNCE_MS && stable != lastReading) {
    stable = lastReading;
    if (stable) {
      pressStart = now;        // Taster gedrückt
    } else {                   // Taster losgelassen
      return ((now - pressStart) >= LONG_PRESS_MS) ? BTN_LONG : BTN_SHORT;
    }
  }
  return BTN_NONE;
}
