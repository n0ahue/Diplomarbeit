#include <Arduino.h>
#include "config.h"
#include "aircraft.h"
#include "data_source.h"
#include "display.h"

static Aircraft ac[MAX_AC];
static char     jsonBuf[JSON_BUF_SIZE];
static uint32_t lastUpdate = 0;

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
  Serial.printf("%d Flugzeuge, freier Heap: %u Byte\n", n, ESP.getFreeHeap());
  displayDrawRadar(ac, n);
}

void setup() {
  Serial.begin(115200);
  displayInit();
  update();
  lastUpdate = millis();
}

void loop() {
  if (millis() - lastUpdate >= REFRESH_MS) {
    lastUpdate = millis();
    update();
  }
}
