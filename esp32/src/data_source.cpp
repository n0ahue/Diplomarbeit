#include "data_source.h"
#include "config.h"
#include "secrets.h"
#include <WiFi.h>
#include <HTTPClient.h>

// Beispiel im Format des api_server.py (nur für USE_SAMPLE_DATA = true)
static const char* SAMPLE_JSON = R"({
 "active_count": 3,
 "data": [
  {"hex":"4B1805","call":"AUA123","alt":36000,"spd":450,"hdg":90.0,"lat":47.3,"lon":15.5,"type":"A320"},
  {"hex":"3C6444","call":"DLH4AB","alt":28000,"spd":420,"hdg":200.0,"lat":46.6,"lon":14.7,"type":"B737"},
  {"hex":"4CA123","call":"N/A","alt":12000,"spd":250,"hdg":330.0,"lat":47.0,"lon":15.2,"type":"GENERIC"}
 ]
})";

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
  if (!USE_SAMPLE_DATA) wifiConnect(10000);
}

bool fetchAircraftJson(char* buf, size_t len) {
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
