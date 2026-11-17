#include "network_task.h"
#include "data_source.h"
#include "config.h"
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

// Erst nach so vielen Fehlversuchen in Folge wird ein Fehler angezeigt;
// bis dahin bleiben die letzten Daten stehen (kurze Aussetzer fallen nicht auf).
constexpr int FAILS_BEFORE_ERROR = 3;

static SemaphoreHandle_t   mutex;
static AircraftSnapshot    shared;   // gemeinsamer Stand, nur mit Mutex benutzen

static void networkTask(void*) {
  static char     jsonBuf[JSON_BUF_SIZE];   // static: liegt nicht auf dem Stack
  static Aircraft local[MAX_AC];
  int fails = 0;
  int lastCount = -1;

  dataSourceInit();   // WLAN verbinden: darf hier ruhig dauern

  for (;;) {
    const char* err = nullptr;
    int n = 0;

    if (!fetchAircraftJson(jsonBuf, sizeof(jsonBuf))) {
      err = "Abruf fehlgeschlagen";
    } else {
      n = parseAircraft(jsonBuf, local, MAX_AC);
      if (n < 0) err = "JSON-Fehler";
    }

    if (err) {
      fails++;
      if (fails >= FAILS_BEFORE_ERROR) {
        xSemaphoreTake(mutex, portMAX_DELAY);
        shared.error = err;
        shared.version++;
        xSemaphoreGive(mutex);
      }
    } else {
      fails = 0;
      xSemaphoreTake(mutex, portMAX_DELAY);
      memcpy(shared.ac, local, sizeof(Aircraft) * n);
      shared.count      = n;
      shared.lastDataMs = millis();
      shared.error      = nullptr;
      shared.version++;
      xSemaphoreGive(mutex);

      if (n != lastCount) {   // nur bei Änderung ausgeben, sonst läuft der Monitor voll
        Serial.printf("%d Flugzeuge, freier Heap: %u Byte\n", n, ESP.getFreeHeap());
        lastCount = n;
      }
    }

    vTaskDelay(pdMS_TO_TICKS(REFRESH_MS));
  }
}

void networkTaskStart() {
  mutex = xSemaphoreCreateMutex();
  shared.error   = "Verbinde WLAN...";   // wird angezeigt, bis die ersten Daten da sind
  shared.version = 1;
  xTaskCreatePinnedToCore(networkTask, "net", 10240, nullptr, 1, nullptr, 0);
}

bool networkGetSnapshot(AircraftSnapshot& out, uint32_t& seenVersion) {
  bool fresh = false;
  xSemaphoreTake(mutex, portMAX_DELAY);
  if (shared.version != seenVersion) {
    out = shared;
    seenVersion = shared.version;
    fresh = true;
  }
  xSemaphoreGive(mutex);
  return fresh;
}
