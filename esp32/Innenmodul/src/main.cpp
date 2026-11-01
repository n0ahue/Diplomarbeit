#include <Arduino.h>
#include <ArduinoJson.h>
#include <U8g2lib.h>
#include <Wire.h>
#include <math.h>

// ---------- Konfiguration ----------
constexpr float HOME_LAT = 47.0f;      // Platzhalter: Antennenstandort anpassen
constexpr float HOME_LON = 15.0f;      // Platzhalter: Antennenstandort anpassen
constexpr float RANGE_KM = 100.0f;     // Radius des Radars
constexpr int   MAX_AC   = 20;         // Max. Flugzeuge im RAM

// Display: bei verschobenem Bild stattdessen SH1106 verwenden
U8G2_SSD1306_128X64_NONAME_F_HW_I2C oled(U8G2_R0, U8X8_PIN_NONE);

// ---------- Datenmodell ----------
struct Aircraft {
  char  hex[7];
  char  call[9];
  int   alt;      // ft
  int   spd;      // kn
  float hdg;      // Grad
  float lat, lon;
};

Aircraft ac[MAX_AC];
int acCount = 0;

// Beispiel im Format deines api_server.py
const char* SAMPLE_JSON = R"({
 "active_count": 3,
 "data": [
  {"hex":"4B1805","call":"AUA123","alt":36000,"spd":450,"hdg":90.0,"lat":47.3,"lon":15.5,"type":"A320"},
  {"hex":"3C6444","call":"DLH4AB","alt":28000,"spd":420,"hdg":200.0,"lat":46.6,"lon":14.7,"type":"B737"},
  {"hex":"4CA123","call":"N/A","alt":12000,"spd":250,"hdg":330.0,"lat":47.0,"lon":15.2,"type":"GENERIC"}
 ]
})";

// ---------- Parsing ----------
bool parseAircraft(const char* json) {
  JsonDocument filter;
  JsonObject f = filter["data"].add<JsonObject>();
  for (const char* k : {"hex","call","alt","spd","hdg","lat","lon"}) f[k] = true;

  JsonDocument doc;
  DeserializationError err =
      deserializeJson(doc, json, DeserializationOption::Filter(filter));
  if (err) {
    Serial.printf("JSON-Fehler: %s\n", err.c_str());
    return false;
  }

  acCount = 0;
  for (JsonObject o : doc["data"].as<JsonArray>()) {
    if (acCount >= MAX_AC) break;
    Aircraft& a = ac[acCount++];
    strlcpy(a.hex,  o["hex"]  | "000000", sizeof(a.hex));
    strlcpy(a.call, o["call"] | "N/A",    sizeof(a.call));
    a.alt = o["alt"] | 0;
    a.spd = o["spd"] | 0;
    a.hdg = o["hdg"] | 0.0f;
    a.lat = o["lat"] | 0.0f;
    a.lon = o["lon"] | 0.0f;
  }
  return true;
}

// ---------- Geometrie: Lat/Lon -> Pixel ----------
void toRadar(float lat, float lon, int cx, int cy, int r, int& px, int& py) {
  float dy = (lat - HOME_LAT) * 111.0f;                       // km nach Norden
  float dx = (lon - HOME_LON) * 111.0f * cosf(HOME_LAT * DEG_TO_RAD); // km nach Osten
  float scale = r / RANGE_KM;
  px = cx + (int)(dx * scale);
  py = cy - (int)(dy * scale);
}

// ---------- Zeichnen ----------
void drawRadar() {
  const int cx = 32, cy = 32, r = 31;
  oled.clearBuffer();
  oled.drawCircle(cx, cy, r);
  oled.drawCircle(cx, cy, r / 2);
  oled.drawPixel(cx, cy);

  for (int i = 0; i < acCount; i++) {
    int px, py;
    toRadar(ac[i].lat, ac[i].lon, cx, cy, r, px, py);
    if ((px - cx) * (px - cx) + (py - cy) * (py - cy) > r * r) continue;
    oled.drawDisc(px, py, 2);
    // Kurzer Strich in Flugrichtung
    float h = ac[i].hdg * DEG_TO_RAD;
    oled.drawLine(px, py, px + (int)(5 * sinf(h)), py - (int)(5 * cosf(h)));
  }

  // Textliste rechts
  oled.setFont(u8g2_font_5x7_tf);
  oled.setCursor(68, 8);
  oled.printf("AC: %d", acCount);
  for (int i = 0; i < acCount && i < 5; i++) {
    oled.setCursor(68, 18 + i * 9);
    oled.printf("%.6s", ac[i].call);
  }
  oled.sendBuffer();
}

void setup() {
  Serial.begin(115200);
  oled.begin();
  if (parseAircraft(SAMPLE_JSON)) {
    Serial.printf("%d Flugzeuge geparst, freier Heap: %u Byte\n",
                  acCount, ESP.getFreeHeap());
  }
  drawRadar();
}

void loop() {}