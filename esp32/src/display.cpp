#include "display.h"
#include "geo.h"
#include "config.h"
#include <U8g2lib.h>
#include <Wire.h>
#include <math.h>

// Bei verschobenem Bild stattdessen SH1106 verwenden
static U8G2_SSD1306_128X64_NONAME_F_HW_I2C oled(U8G2_R0, U8X8_PIN_NONE);

static inline int ri(float v) { return (int)lroundf(v); }

void displayInit() {
  oled.setBusClock(400000);   // schneller I2C-Takt, damit mehr Bilder pro Sekunde möglich sind
  oled.begin();
}

// Position nach dtSec Sekunden Geradeausflug (Dead Reckoning)
static void predictPos(const Aircraft& a, float dtSec, float& lat, float& lon) {
  const float distKm = a.spd * 0.000514444f * dtSec;   // Knoten -> km/s
  const float h = a.hdg * DEG_TO_RAD;
  lat = a.lat + cosf(h) * distKm / 111.0f;
  lon = a.lon + sinf(h) * distKm / (111.0f * cosf(a.lat * DEG_TO_RAD));
}

// Kleines Flugzeugsymbol (Rumpf, Flügel, Leitwerk), gedreht nach dem Kurs
static void drawPlane(int x, int y, float hdgDeg) {
  const float h = hdgDeg * DEG_TO_RAD;
  const float s = sinf(h), c = cosf(h);   // Vorwärts = (s, -c), Seitwärts = (c, s)

  // Rumpf: von der Nase zum Heck
  oled.drawLine(x + ri(4 * s), y - ri(4 * c), x - ri(3 * s), y + ri(3 * c));
  // Flügel: quer, etwas vor der Mitte
  const int wx = x + ri(1 * s), wy = y - ri(1 * c);
  oled.drawLine(wx - ri(3 * c), wy - ri(3 * s), wx + ri(3 * c), wy + ri(3 * s));
  // Leitwerk: kurz, am Heck
  const int ex = x - ri(2 * s), ey = y + ri(2 * c);
  oled.drawLine(ex - ri(2 * c), ey - ri(2 * s), ex + ri(2 * c), ey + ri(2 * s));
}

void displayDrawRadar(const Aircraft* ac, int count, float dtSec) {
  const int cx = 32, cy = 32, r = 31;
  oled.clearBuffer();

  // Radar-Hintergrund: Außenring, Halbring, Standortkreuz, Nordmarke
  oled.drawCircle(cx, cy, r);
  oled.drawCircle(cx, cy, r / 2);
  oled.drawHLine(cx - 2, cy, 5);   // Standort als Kreuz
  oled.drawVLine(cx, cy - 2, 5);
  oled.setFont(u8g2_font_4x6_tf);
  oled.setCursor(cx - 1, cy - r + 8);
  oled.print("N");

  // Fortgeschriebene Positionen und Entfernungen berechnen
  float plat[MAX_AC], plon[MAX_AC], dist[MAX_AC];
  int   idx[MAX_AC];
  for (int i = 0; i < count; i++) {
    predictPos(ac[i], dtSec, plat[i], plon[i]);
    dist[i] = distanceKm(plat[i], plon[i]);
    idx[i]  = i;
  }
  for (int i = 1; i < count; i++) {          // nach Entfernung sortieren (Insertion Sort)
    int k = idx[i];
    int j = i - 1;
    while (j >= 0 && dist[idx[j]] > dist[k]) {
      idx[j + 1] = idx[j];
      j--;
    }
    idx[j + 1] = k;
  }

  // Flugzeuge zeichnen, das nächste bekommt einen Rahmen
  for (int n = 0; n < count; n++) {
    const int i = idx[n];
    int px, py;
    toRadar(plat[i], plon[i], cx, cy, r - 4, px, py);   // etwas Rand lassen
    if ((px - cx) * (px - cx) + (py - cy) * (py - cy) > r * r) continue;
    drawPlane(px, py, ac[i].hdg);
    //if (n == 0) oled.drawFrame(px - 5, py - 5, 11, 11);
  }

  // Rechte Spalte: Anzahl, Radius, die fünf nächsten Flugzeuge mit Flugfläche
  oled.setFont(u8g2_font_5x7_tf);
  oled.setCursor(68, 8);
  oled.printf("AC: %d", count);
  oled.setCursor(68, 17);
  oled.printf("R:%dkm", (int)RANGE_KM);
  for (int n = 0; n < count && n < 5; n++) {
    const Aircraft& a = ac[idx[n]];
    int fl = a.alt / 100;
    if (fl > 999) fl = 999;
    oled.setCursor(68, 27 + n * 8);
    oled.printf("%.6s F%03d", a.call, fl);
  }
  oled.sendBuffer();
}

void displayDrawDetail(const Aircraft* ac, int count, int selected) {
  oled.clearBuffer();
  oled.setFont(u8g2_font_6x10_tf);

  if (count == 0) {
    oled.setCursor(0, 12);
    oled.print("Keine Flugzeuge");
    oled.sendBuffer();
    return;
  }

  const Aircraft& a = ac[selected];
  oled.setCursor(0, 10);
  oled.printf("%.8s  %d/%d", a.call, selected + 1, count);
  oled.setCursor(0, 22);
  oled.printf("Hex: %s", a.hex);
  oled.setCursor(0, 33);
  oled.printf("Alt: %d ft", a.alt);
  oled.setCursor(0, 44);
  oled.printf("Spd: %d kn", a.spd);
  oled.setCursor(0, 55);
  oled.printf("Hdg:%.0f  %.0f km", a.hdg, distanceKm(a.lat, a.lon));
  oled.sendBuffer();
}

void displayShowError(const char* msg) {
  oled.clearBuffer();
  oled.setFont(u8g2_font_5x7_tf);
  oled.setCursor(0, 10);
  oled.print(msg);
  oled.sendBuffer();
}
