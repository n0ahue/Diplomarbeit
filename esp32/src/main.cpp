#include <Arduino.h>
#include "config.h"
#include "aircraft.h"
#include "data_source.h"
#include "display.h"
#include "button.h"

// Bedienung: Jeder Tastendruck schaltet eine Ansicht weiter:
// Radar -> Flugzeug 1 -> Flugzeug 2 -> ... -> letztes Flugzeug -> Radar -> ...

static Aircraft ac[MAX_AC];
static int      acCount    = 0;
static char     jsonBuf[JSON_BUF_SIZE];
static uint32_t lastUpdate = 0;

static bool detail   = false;  // false = Radar, true = Detailansicht
static int  selected = 0;      // Index des angezeigten Flugzeugs
static char selHex[7] = "";    // Hex-Code merkt sich das Flugzeug über Updates hinweg

static void render() {
  if (detail) displayDrawDetail(ac, acCount, selected);
  else        displayDrawRadar(ac, acCount);
}

static void selectAircraft(int idx) {
  selected = idx;
  strlcpy(selHex, ac[idx].hex, sizeof(selHex));
}

static void onPress() {
  if (!detail) {
    if (acCount > 0) {          // Radar -> erstes Flugzeug
      detail = true;
      selectAircraft(0);
    }
  } else if (selected + 1 < acCount) {
    selectAircraft(selected + 1);  // nächstes Flugzeug
  } else {
    detail = false;                // nach dem letzten zurück zum Radar
  }
  render();
}

static void update() {
  if (!fetchAircraftJson(jsonBuf, sizeof(jsonBuf))) {
    displayShowError("Abruf fehlgeschlagen");
    return;
  }
  int n = parseAircraft(jsonBuf, ac, MAX_AC);
  if (n < 0) {
    displayShowError("JSON-Fehler");
    return;
  }
  acCount = n;

  // Angezeigtes Flugzeug nach dem Update wiederfinden (Reihenfolge kann wechseln)
  if (detail) {
    bool found = false;
    for (int i = 0; i < acCount; i++) {
      if (strcmp(ac[i].hex, selHex) == 0) {
        selected = i;
        found = true;
        break;
      }
    }
    if (!found) detail = false;    // Flugzeug nicht mehr da -> zurück zum Radar
  }

  Serial.printf("%d Flugzeuge, freier Heap: %u Byte\n", acCount, ESP.getFreeHeap());
  render();
}

void setup() {
  Serial.begin(115200);
  displayInit();
  buttonInit();
  displayShowError("Verbinde WLAN...");
  dataSourceInit();
  update();
  lastUpdate = millis();
}

void loop() {
  if (buttonPoll() == BTN_PRESSED) {
    onPress();
  }

  if (millis() - lastUpdate >= REFRESH_MS) {
    lastUpdate = millis();
    update();
  }
}
