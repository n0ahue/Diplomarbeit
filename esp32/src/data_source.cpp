#include "data_source.h"
#include "config.h"
#include "secrets.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <math.h>

// ---------- Beispieldaten (USE_SAMPLE_DATA) ----------
static const char* SAMPLE_JSON = R"({
 "active_count": 3,
 "data": [
  {"hex":"4B1805","call":"AUA123","alt":36000,"spd":450,"hdg":90.0,"lat":47.3,"lon":15.5,"type":"A320"},
  {"hex":"3C6444","call":"DLH4AB","alt":28000,"spd":420,"hdg":200.0,"lat":46.6,"lon":14.7,"type":"B737"},
  {"hex":"4CA123","call":"N/A","alt":12000,"spd":250,"hdg":330.0,"lat":47.0,"lon":15.2,"type":"GENERIC"}
 ]
})";

// ---------- Simulation (USE_SIMULATION) ----------
// Testflugzeuge fliegen geradeaus um den Antennenstandort und tauchen
// am gegenüberliegenden Rand wieder auf.
struct SimAc {
  const char* hex;
  const char* call;
  int   alt;     // ft
  int   spd;     // kn
  float hdg;     // Grad
  float x0, y0;  // Startposition in km (Ost / Nord) relativ zum Standort
};

static const SimAc SIM[] = {
  {"4B1805", "AUA123", 36000, 450,  90.0f, -60.0f,  20.0f},
  {"3C6444", "DLH4AB", 28000, 420, 200.0f,  20.0f,  60.0f},
  {"4CA123", "RYR7XZ", 12000, 250, 330.0f,  30.0f, -50.0f},
  {"440123", "EZY81B",  8000, 180,  45.0f, -70.0f, -60.0f},
  {"4A5AAB", "SWR19K", 30000, 480, 270.0f,  80.0f, -10.0f},
};

static float wrapKm(float v) {
  float w = fmodf(v + RANGE_KM, 2.0f * RANGE_KM);
  if (w < 0) w += 2.0f * RANGE_KM;
  return w - RANGE_KM;
}

static bool simulateJson(char* buf, size_t len) {
  const float t = (millis() / 1000.0f) * SIM_SPEEDUP;   // simulierte Sekunden
  const int count = sizeof(SIM) / sizeof(SIM[0]);
  const float kmPerDegLon = 111.0f * cosf(HOME_LAT * DEG_TO_RAD);

  int n = snprintf(buf, len, "{\"active_count\":%d,\"data\":[", count);
  for (int i = 0; i < count && n < (int)len; i++) {
    const SimAc& a = SIM[i];
    const float dist = a.spd * 0.000514444f * t;          // km seit Start
    const float h = a.hdg * DEG_TO_RAD;
    const float x = wrapKm(a.x0 + sinf(h) * dist);
    const float y = wrapKm(a.y0 + cosf(h) * dist);
    n += snprintf(buf + n, len - n,
      "%s{\"hex\":\"%s\",\"call\":\"%s\",\"alt\":%d,\"spd\":%d,\"hdg\":%.1f,"
      "\"lat\":%.4f,\"lon\":%.4f,\"type\":\"SIM\"}",
      i ? "," : "", a.hex, a.call, a.alt, a.spd, a.hdg,
      HOME_LAT + y / 111.0f, HOME_LON + x / kmPerDegLon);
  }
  if (n < (int)len) snprintf(buf + n, len - n, "]}");
  return n < (int)len;
}

// ---------- WLAN-Abruf vom Pi ----------
static bool wifiConnect(uint32_t timeoutMs) {
  if (WiFi.status() == WL_CONNECTED) return true;
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  const uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - t0) < timeoutMs) {
    delay(250);
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("WLAN verbunden, IP: %s\n", WiFi.localIP().toString().c_str());
    return true;
  }
  Serial.println("WLAN-Verbindung fehlgeschlagen");
  return false;
}

void dataSourceInit() {
  if (!USE_SIMULATION && !USE_SAMPLE_DATA) wifiConnect(10000);
}

bool fetchAircraftJson(char* buf, size_t len) {
  if (USE_SIMULATION) return simulateJson(buf, len);

  if (USE_SAMPLE_DATA) {
    strlcpy(buf, SAMPLE_JSON, len);
    return true;
  }

  if (!wifiConnect(3000)) return false;

  HTTPClient http;
  http.setTimeout(3000);
  const String url = String("http://") + PI_HOST + ":" + String(PI_PORT) + PI_PATH;
  if (!http.begin(url)) {
    Serial.println("HTTP begin fehlgeschlagen");
    return false;
  }

  const int code = http.GET();
  if (code != HTTP_CODE_OK) {
    Serial.printf("HTTP-Fehler: %d\n", code);
    http.end();
    return false;
  }

  const String body = http.getString();
  http.end();

  if (body.length() >= len) {
    Serial.printf("Antwort zu groß: %u Byte\n", (unsigned)body.length());
    return false;
  }
  strlcpy(buf, body.c_str(), len);
  return true;
}
