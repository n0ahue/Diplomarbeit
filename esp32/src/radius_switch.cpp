#include "radius_switch.h"
#include "config.h"

void radiusSwitchInit() {
  pinMode(SWITCH_PIN, INPUT_PULLUP);   // Schalter schaltet gegen GND
  delay(10);                           // kurz warten, bis der Pull-up stabil ist
}

float radiusSwitchKm() {
  static bool     initialized = false;
  static bool     lastReading = false;
  static bool     stable      = false;
  static uint32_t lastChange  = 0;

  const bool     closed = (digitalRead(SWITCH_PIN) == LOW);
  const uint32_t now    = millis();

  if (!initialized) {                  // erster Aufruf: Startzustand sofort übernehmen
    initialized = true;
    lastReading = stable = closed;
    lastChange  = now;
  }

  if (closed != lastReading) {         // Stellung hat gewechselt: Entprellzeit neu starten
    lastReading = closed;
    lastChange  = now;
  }
  if ((now - lastChange) >= SWITCH_DEBOUNCE_MS) {
    stable = lastReading;
  }

  return stable ? RADIUS_CLOSED_KM : RADIUS_OPEN_KM;
}
