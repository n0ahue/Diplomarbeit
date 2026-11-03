#include "button.h"
#include "config.h"

void buttonInit() {
  pinMode(BUTTON_PIN, INPUT_PULLUP);  // Taster schaltet gegen GND
}

ButtonEvent buttonPoll() {
  static bool     lastReading = false;
  static bool     stable      = false;
  static uint32_t lastChange  = 0;
  static uint32_t lastEvent   = 0;

  const bool     down = (digitalRead(BUTTON_PIN) == LOW);
  const uint32_t now  = millis();

  if (down != lastReading) {   // Zustand hat gewechselt: Entprellzeit neu starten
    lastReading = down;
    lastChange  = now;
  }

  if ((now - lastChange) >= DEBOUNCE_MS && stable != lastReading) {
    stable = lastReading;
    // Nur das Drücken zählt, nicht das Loslassen. Zusätzlich Sperrzeit gegen Prellen.
    if (stable && (now - lastEvent) >= MIN_PRESS_GAP_MS) {
      lastEvent = now;
      return BTN_PRESSED;
    }
  }
  return BTN_NONE;
}
