#include "aircraft.h"
#include <ArduinoJson.h>

int parseAircraft(const char* json, Aircraft* out, int maxCount) {
  // Filter: nur die Felder behalten, die wir wirklich brauchen (spart RAM)
  JsonDocument filter;
  JsonObject f = filter["data"].add<JsonObject>();
  for (const char* k : {"hex", "call", "alt", "spd", "hdg", "lat", "lon"}) {
    f[k] = true;
  }

  JsonDocument doc;
  DeserializationError err =
      deserializeJson(doc, json, DeserializationOption::Filter(filter));
  if (err) {
    Serial.printf("JSON-Fehler: %s\n", err.c_str());
    return -1;
  }

  int count = 0;
  for (JsonObject o : doc["data"].as<JsonArray>()) {
    if (count >= maxCount) break;
    Aircraft& a = out[count++];
    strlcpy(a.hex,  o["hex"]  | "000000", sizeof(a.hex));
    strlcpy(a.call, o["call"] | "N/A",    sizeof(a.call));
    a.alt = (int)lroundf(o["alt"].as<float>());
    a.spd = (int)lroundf(o["spd"].as<float>());   
    a.hdg = o["hdg"] | 0.0f;
    a.lat = o["lat"] | 0.0f;
    a.lon = o["lon"] | 0.0f;
  }
  return count;
}
