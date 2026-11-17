#include <Arduino.h>
#include "config.h"
#include "aircraft.h"
#include "network_task.h"
#include "display.h"
#include "button.h"
#include "radius_switch.h"

// Bedienung:
//   Taster:   jeder Druck schaltet eine Ansicht weiter:
//             Radar -> Flugzeug 1 -> Flugzeug 2 -> ... -> letztes Flugzeug -> Radar -> ...
//   Schalter: wählt den Radar-Radius (100 km oder 50 km, siehe config.h)
//
// Das Netzwerk läuft in einem eigenen Task (network_task.cpp).
// Diese Schleife kümmert sich nur um Taster, Schalter und Anzeige und wartet nie auf das Netz.

static AircraftSnapshot snap;        // lokale Kopie der aktuellen Daten
static uint32_t seenVersion = 0;

static uint32_t lastFrame = 0;       // letztes gezeichnetes Bild
static float    rangeKm   = RADIUS_OPEN_KM;   // aktueller Radar-Radius (vom Schalter bestimmt)

static bool detail    = false;       // false = Radar, true = Detailansicht
static int  selected  = 0;           // Index des angezeigten Flugzeugs
static char selHex[7] = "";          // Hex-Code merkt sich das Flugzeug über Updates hinweg

static void render() {
  lastFrame = millis();
  if (snap.error) {
    displayShowError(snap.error);
  } else if (detail) {
    displayDrawDetail(snap.ac, snap.count, selected);
  } else {
    const float ageSec = (millis() - snap.lastDataMs) / 1000.0f;   // echtes Alter der Daten
    const float dt = (ageSec > MAX_EXTRAPOLATION_S ? MAX_EXTRAPOLATION_S : ageSec) * TIME_SCALE;
    displayDrawRadar(snap.ac, snap.count, dt, rangeKm, ageSec);
  }
}

static void selectAircraft(int idx) {
  selected = idx;
  strlcpy(selHex, snap.ac[idx].hex, sizeof(selHex));
}

static void onPress() {
  if (!detail) {
    if (snap.count > 0) {          // Radar -> erstes Flugzeug
      detail = true;
      selectAircraft(0);
    }
  } else if (selected + 1 < snap.count) {
    selectAircraft(selected + 1);  // nächstes Flugzeug
  } else {
    detail = false;                // nach dem letzten zurück zum Radar
  }
  render();
}

// Neue Daten vom Netzwerk-Task sind angekommen
static void onNewData() {
  if (detail) {
    // Angezeigtes Flugzeug wiederfinden (die Reihenfolge kann wechseln)
    bool found = false;
    for (int i = 0; i < snap.count; i++) {
      if (strcmp(snap.ac[i].hex, selHex) == 0) {
        selected = i;
        found = true;
        break;
      }
    }
    if (!found) detail = false;    // Flugzeug nicht mehr da -> zurück zum Radar
  }
  render();
}

void setup() {
  Serial.begin(115200);
  displayInit();
  buttonInit();
  radiusSwitchInit();
  rangeKm = radiusSwitchKm();
  displayShowError("Starte...");
  networkTaskStart();
}

void loop() {
  if (buttonPoll() == BTN_PRESSED) {
    onPress();
  }

  // Radius-Schalter: bei Änderung sofort neu zeichnen
  const float r = radiusSwitchKm();
  if (r != rangeKm) {
    rangeKm = r;
    Serial.printf("Radius: %d km\n", (int)rangeKm);
    render();
  }

  if (networkGetSnapshot(snap, seenVersion)) {
    onNewData();                                 // neue Daten: neu zeichnen
  } else if (!detail && !snap.error && millis() - lastFrame >= FRAME_MS) {
    render();                                    // Radar zwischen den Updates fortschreiben
  }
}
