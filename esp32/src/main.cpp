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

static uint32_t lastUpdate = 0;   // letzter Datenabruf
static uint32_t lastData   = 0;   // Zeitpunkt der letzten erfolgreichen Daten
static uint32_t lastFrame  = 0;   // letztes gezeichnetes Bild

static bool        detail   = false;   // false = Radar, true = Detailansicht
static int         selected = 0;       // Index des angezeigten Flugzeugs
static char        selHex[7] = "";     // Hex-Code merkt sich das Flugzeug über Updates hinweg
static const char* errorMsg = nullptr; // gesetzt, wenn der letzte Abruf fehlschlug

static void render() {
  lastFrame = millis();
  if (errorMsg) {
    displayShowError(errorMsg);
  } else if (detail) {
    displayDrawDetail(ac, acCount, selected);
  } else {
    float dt = (millis() - lastData) / 1000.0f;
    if (dt > MAX_EXTRAPOLATION_S) dt = MAX_EXTRAPOLATION_S;
    displayDrawRadar(ac, acCount, dt * TIME_SCALE);
  }
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
    errorMsg = "Abruf fehlgeschlagen";
    render();
    return;
  }
  int n = parseAircraft(jsonBuf, ac, MAX_AC);
  if (n < 0) {
    errorMsg = "JSON-Fehler";
    render();
    return;
  }
  errorMsg = nullptr;
  acCount  = n;
  lastData = millis();

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

  const uint32_t now = millis();
  if (now - lastUpdate >= REFRESH_MS) {
    lastUpdate = now;
    update();                                   // holt Daten und zeichnet danach neu
  } else if (!detail && !errorMsg && now - lastFrame >= FRAME_MS) {
    render();                                   // Radar zwischen den Updates fortschreiben
  }
}
