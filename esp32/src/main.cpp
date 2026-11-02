#include <Arduino.h>
#include "config.h"
#include "aircraft.h"
#include "data_source.h"
#include "display.h"
#include "button.h"

enum Page { PAGE_RADAR, PAGE_DETAIL, PAGE_COUNT };

static Aircraft ac[MAX_AC];
static int      acCount    = 0;
static char     jsonBuf[JSON_BUF_SIZE];
static uint32_t lastUpdate = 0;
static int      page       = PAGE_RADAR;
static int      selected   = 0;   // Auswahl auf der Detailseite

static void render() {
  if (page == PAGE_RADAR) displayDrawRadar(ac, acCount);
  else                    displayDrawDetail(ac, acCount, selected);
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
  if (selected >= acCount) selected = 0;
  Serial.printf("%d Flugzeuge, freier Heap: %u Byte\n", acCount, ESP.getFreeHeap());
  render();
}

void setup() {
  Serial.begin(115200);
  displayInit();
  buttonInit();
  update();
  lastUpdate = millis();
}

void loop() {
  switch (buttonPoll()) {
    case BTN_SHORT:   // kurzer Druck: Seite wechseln
      page = (page + 1) % PAGE_COUNT;
      Serial.printf("Seite %d\n", page);
      render();
      break;
    case BTN_LONG:    // langer Druck: auf der Detailseite nächstes Flugzeug
      if (page == PAGE_DETAIL && acCount > 0) {
        selected = (selected + 1) % acCount;
      }
      Serial.println("Langer Druck");
      render();
      break;
    default:
      break;
  }

  if (millis() - lastUpdate >= REFRESH_MS) {
    lastUpdate = millis();
    update();
  }
}
