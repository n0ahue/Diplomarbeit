#include "button.h"
#include "config.h"

void buttonInit() {
  pinMode(BUTTON_PIN, INPUT_PULLUP);  // Taster schaltet gegen GND
}

ButtonEvent buttonPoll() {
  static bool     lastReading = false;
  static bool     stable      = false;
  static bool     longFired   = false;
  static uint32_t lastChange  = 0;
  static uint32_t pressStart  = 0;
  static uint32_t lastEvent   = 0;

  const bool     down = (digitalRead(BUTTON_PIN) == LOW);
  const uint32_t now  = millis();

  if (down != lastReading) {   // Zustand hat gewechselt: Entprellzeit neu starten
    lastReading = down;
    lastChange  = now;
  }

  if ((now - lastChange) >= DEBOUNCE_MS && stable != lastReading) {
    stable = lastReading;
    if (stable) {                       // gedrückt
      pressStart = now;
      longFired  = false;
    } else if (!longFired && (now - lastEvent) >= MIN_PRESS_GAP_MS) {
      lastEvent = now;                  // losgelassen, ohne dass "lang" schon kam
      return BTN_SHORT;
    }
  }

  // Langer Druck: sofort melden, sobald die Haltezeit erreicht ist
  if (stable && !longFired && (now - pressStart) >= LONG_PRESS_MS) {
    longFired = true;
    lastEvent = now;
    return BTN_LONG;
  }
  return BTN_NONE;
}
