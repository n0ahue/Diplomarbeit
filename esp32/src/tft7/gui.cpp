#include <Arduino.h>
#include <math.h>
#include <Fonts/FreeSans9pt7b.h>
#include <Fonts/FreeSansBold12pt7b.h>
#include "gui.h"
#include "config.h"
#include "geo.h"
#include "airlines.h"

// ---------- Farben ----------
constexpr uint16_t COL_BG     = rgb565(6, 12, 22);
constexpr uint16_t COL_RING   = rgb565(30, 70, 90);
constexpr uint16_t COL_RING2  = rgb565(18, 40, 55);
constexpr uint16_t COL_TEXT   = rgb565(225, 232, 240);
constexpr uint16_t COL_DIM    = rgb565(120, 140, 160);
constexpr uint16_t COL_PANEL  = rgb565(14, 22, 34);
constexpr uint16_t COL_ROW    = rgb565(24, 36, 52);
constexpr uint16_t COL_SEL    = rgb565(255, 170, 0);
constexpr uint16_t COL_BTN    = rgb565(30, 46, 66);
constexpr uint16_t COL_ACCENT = rgb565(0, 150, 200);
constexpr uint16_t COL_OK     = rgb565(0, 220, 120);
constexpr uint16_t COL_WARN   = rgb565(255, 170, 0);
constexpr uint16_t COL_ERR    = rgb565(240, 70, 60);

// ---------- Layout ----------
constexpr int RADAR_W = 480;                // Radarfläche: x 0..479, y 0..479
constexpr int RADAR_CX = 240, RADAR_CY = 240, RADAR_R = 225;
constexpr int PANEL_X = 480, PANEL_W = 320;
constexpr int BTN_Y = 52, BTN_H = 44, BTN_W = 72, BTN_STEP = 78;
constexpr int ROW_Y = 112, ROW_H = 48, ROW_STEP = 52, ROW_COUNT = 5;
constexpr int CARD_Y = 380;

// Radien der vier Tasten (km) und die beim Start gewählte Taste
static const float RANGES[4] = {25.0f, 50.0f, 100.0f, 150.0f};
static int rangeIdx = 2;

// ---------- Zustand ----------
static Lcd7*            lcd = nullptr;
static AircraftSnapshot snap;                 // letzte Daten vom Netzwerk-Task
static float            vx[MAX_AC], vy[MAX_AC], vd[MAX_AC];   // aktuelle Position (km Ost/Nord) und Entfernung
static int              order[MAX_AC];        // Flugzeuge nach Entfernung sortiert
static int              selIdx = -1;          // Index in snap.ac, -1 = keines
static char             selHex[7] = "";       // merkt sich das Flugzeug über Datenupdates hinweg
static float            ageSec = 0;           // Alter der Daten in Sekunden
static bool             panelDirty = true;
static uint32_t         lastPanel = 0;

static float rangeKm() { return RANGES[rangeIdx]; }

static uint16_t altColor(int altFt) {
  if (altFt < 10000) return rgb565(255, 200, 0);      // tief: gelb
  if (altFt < 25000) return rgb565(0, 220, 120);      // mittel: grün
  return rgb565(0, 200, 255);                          // hoch: cyan
}

static const char* labelOf(const Aircraft& a) {
  return strcmp(a.call, "N/A") == 0 ? a.hex : a.call;
}

// ---------- Berechnung ----------
static void updateView() {
  ageSec = (millis() - snap.lastDataMs) / 1000.0f;
  float dt = (ageSec > MAX_EXTRAPOLATION_S ? MAX_EXTRAPOLATION_S : ageSec) * TIME_SCALE;

  for (int i = 0; i < snap.count; i++) {
    const Aircraft& a = snap.ac[i];
    // Position entlang Kurs und Geschwindigkeit fortschreiben (flüssige Bewegung zwischen Updates)
    const float dKm = a.spd * 0.000514444f * dt;
    const float h = a.hdg * DEG_TO_RAD;
    const float lat = a.lat + cosf(h) * dKm / 111.0f;
    const float lon = a.lon + sinf(h) * dKm / (111.0f * cosf(a.lat * DEG_TO_RAD));
    offsetsKm(lat, lon, vx[i], vy[i]);
    vd[i] = sqrtf(vx[i] * vx[i] + vy[i] * vy[i]);
    order[i] = i;
  }
  for (int i = 1; i < snap.count; i++) {              // nach Entfernung sortieren
    int k = order[i], j = i - 1;
    while (j >= 0 && vd[order[j]] > vd[k]) { order[j + 1] = order[j]; j--; }
    order[j + 1] = k;
  }

  // Ausgewähltes Flugzeug über den Hex-Code wiederfinden
  selIdx = -1;
  if (selHex[0]) {
    for (int i = 0; i < snap.count; i++) {
      if (strcmp(snap.ac[i].hex, selHex) == 0) { selIdx = i; break; }
    }
    if (selIdx < 0) { selHex[0] = 0; panelDirty = true; }   // Flugzeug nicht mehr da
  }
}

static void planePos(int i, int& px, int& py) {
  px = RADAR_CX + (int)lroundf(vx[i] * RADAR_R / rangeKm());
  py = RADAR_CY - (int)lroundf(vy[i] * RADAR_R / rangeKm());
}

// ---------- Zeichnen: Radar ----------
static void rotPoint(float lx, float ly, float c, float s, int cx, int cy, int16_t& ox, int16_t& oy) {
  ox = cx + (int16_t)lroundf(lx * c - ly * s);
  oy = cy + (int16_t)lroundf(lx * s + ly * c);
}

static void drawPlane(Stripe& g, int cx, int cy, float hdgDeg, uint16_t col) {
  const float h = hdgDeg * DEG_TO_RAD;
  const float c = cosf(h), s = sinf(h);
  // Umriss aus Dreiecken, die Nase zeigt bei Kurs 0 nach oben
  static const float T[][6] = {
    {0, -13,   3, 7,  -3, 7},   // Rumpf
    {0,  -3,  13, 5,   0, 2},   // Flügel rechts
    {0,  -3, -13, 5,   0, 2},   // Flügel links
    {0,   4,   6, 9,   0, 7},   // Leitwerk rechts
    {0,   4,  -6, 9,   0, 7},   // Leitwerk links
  };
  for (const auto& t : T) {
    int16_t x0, y0, x1, y1, x2, y2;
    rotPoint(t[0], t[1], c, s, cx, cy, x0, y0);
    rotPoint(t[2], t[3], c, s, cx, cy, x1, y1);
    rotPoint(t[4], t[5], c, s, cx, cy, x2, y2);
    g.fillTriangle(x0, y0, x1, y1, x2, y2, col);
  }
}

static void radarScene(Stripe& g) {
  // Ringe und Achsen
  for (int i = 1; i <= 4; i++) {
    g.drawCircle(RADAR_CX, RADAR_CY, RADAR_R * i / 4, i == 4 ? COL_RING : COL_RING2);
  }
  g.drawFastHLine(RADAR_CX - RADAR_R, RADAR_CY, 2 * RADAR_R, COL_RING2);
  g.drawFastVLine(RADAR_CX, RADAR_CY - RADAR_R, 2 * RADAR_R, COL_RING2);

  // Beschriftung der Ringe in km
  g.setFont(&FreeSans9pt7b);
  g.setTextColor(COL_DIM);
  for (int i = 1; i <= 4; i++) {
    const int y = RADAR_CY - RADAR_R * i / 4;
    if (!g.overlaps(y - 18, 22)) continue;
    g.setCursor(RADAR_CX + 5, y - 3);
    g.printf("%d", (int)(rangeKm() * i / 4));
  }

  // Nordmarke
  g.setFont(&FreeSansBold12pt7b);
  g.setTextColor(COL_TEXT);
  if (g.overlaps(RADAR_CY - RADAR_R + 4, 24)) {
    g.setCursor(RADAR_CX - 8, RADAR_CY - RADAR_R + 24);
    g.print("N");
  }

  // Standort
  g.fillCircle(RADAR_CX, RADAR_CY, 4, COL_TEXT);

  // Flugzeuge
  g.setFont(&FreeSans9pt7b);
  for (int i = 0; i < snap.count; i++) {
    const Aircraft& a = snap.ac[i];
    int px, py;
    planePos(i, px, py);
    const int dx = px - RADAR_CX, dy = py - RADAR_CY;
    if (dx * dx + dy * dy > RADAR_R * RADAR_R) continue;     // außerhalb des Radars
    if (!g.overlaps(py - 26, 52)) continue;                  // berührt diesen Streifen nicht
    const uint16_t col = altColor(a.alt);
    drawPlane(g, px, py, a.hdg, col);
    g.setTextColor(col);
    g.setCursor(px + 18, py + 5);
    g.print(labelOf(a));
    if (i == selIdx) {
      g.drawCircle(px, py, 20, COL_SEL);
      g.drawCircle(px, py, 21, COL_SEL);
    }
  }
}

// ---------- Zeichnen: Bedienleiste ----------
static void printRight(Stripe& g, int xRight, int y, const char* s) {
  int16_t bx, by; uint16_t bw, bh;
  g.getTextBounds(s, 0, 0, &bx, &by, &bw, &bh);
  g.setCursor(xRight - bw - bx, y);
  g.print(s);
}

static void printCentered(Stripe& g, int cx, int y, const char* s) {
  int16_t bx, by; uint16_t bw, bh;
  g.getTextBounds(s, 0, 0, &bx, &by, &bw, &bh);
  g.setCursor(cx - (int)bw / 2 - bx, y);
  g.print(s);
}

static void panelScene(Stripe& g) {
  g.fillRect(PANEL_X, 0, PANEL_W, LCD_H, COL_PANEL);

  // Titel
  g.setFont(&FreeSansBold12pt7b);
  g.setTextColor(COL_TEXT);
  g.setCursor(PANEL_X + 12, 32);
  g.print("Flugradar");

  // Status: Punkt und kurzer Text (rot = Fehler, orange = Daten veraltet, grün = in Ordnung)
  uint16_t dot = COL_OK;
  const char* status = nullptr;
  if (snap.error) {
    dot = COL_ERR;
    status = (strncmp(snap.error, "Verbinde", 8) == 0) ? "Verbinde WLAN..." : "Keine Verbindung";
  } else if (ageSec > STALE_DATA_S) {
    dot = COL_WARN;
    status = "Daten veraltet";
  }
  g.fillCircle(PANEL_X + PANEL_W - 20, 22, 7, dot);
  if (status) {
    g.setFont(&FreeSans9pt7b);
    g.setTextColor(dot);
    printRight(g, PANEL_X + PANEL_W - 34, 27, status);
  }

  // Radius-Tasten
  g.setFont(&FreeSans9pt7b);
  for (int i = 0; i < 4; i++) {
    const int x = PANEL_X + 8 + i * BTN_STEP;
    g.fillRoundRect(x, BTN_Y, BTN_W, BTN_H, 8, i == rangeIdx ? COL_ACCENT : COL_BTN);
    char buf[12];
    snprintf(buf, sizeof(buf), "%d km", (int)RANGES[i]);
    g.setTextColor(COL_TEXT);
    printCentered(g, x + BTN_W / 2, BTN_Y + BTN_H / 2 + 6, buf);
  }

  g.setTextColor(COL_DIM);
  g.setCursor(PANEL_X + 12, ROW_Y - 8);
  g.printf("Naechste Flugzeuge (%d)", snap.count);

  // Liste, nach Entfernung sortiert
  if (snap.count == 0) {
    g.setTextColor(COL_DIM);
    g.setCursor(PANEL_X + 18, ROW_Y + 40);
    g.print("Keine Flugzeuge in Reichweite");
  }
  for (int r = 0; r < snap.count && r < ROW_COUNT; r++) {
    const int i = order[r];
    const Aircraft& a = snap.ac[i];
    const int y = ROW_Y + r * ROW_STEP;
    const bool sel = (i == selIdx);
    g.fillRoundRect(PANEL_X + 8, y, PANEL_W - 16, ROW_H, 6, sel ? COL_SEL : COL_ROW);
    if (sel) g.fillRoundRect(PANEL_X + 11, y + 3, PANEL_W - 22, ROW_H - 6, 4, COL_ROW);

    g.setFont(&FreeSansBold12pt7b);
    g.setTextColor(altColor(a.alt));
    g.setCursor(PANEL_X + 18, y + 22);
    g.print(labelOf(a));

    g.setFont(&FreeSans9pt7b);
    g.setTextColor(COL_DIM);
    g.setCursor(PANEL_X + 18, y + 41);
    const char* airline = airlineName(a.call);
    if (airline) g.print(airline); else g.printf("Hex %s", a.hex);

    int fl = a.alt / 100;
    if (fl < 0) fl = 0;
    if (fl > 999) fl = 999;
    char flText[12], dText[12];
    snprintf(flText, sizeof(flText), "FL%03d", fl);
    snprintf(dText, sizeof(dText), "%d km", (int)lroundf(vd[i]));
    g.setTextColor(COL_TEXT);
    printRight(g, PANEL_X + PANEL_W - 18, y + 22, flText);
    g.setTextColor(COL_DIM);
    printRight(g, PANEL_X + PANEL_W - 18, y + 41, dText);
  }

  // Detailkarte des ausgewählten Flugzeugs
  g.fillRoundRect(PANEL_X + 8, CARD_Y, PANEL_W - 16, 92, 8, COL_ROW);
  if (selIdx >= 0) {
    const Aircraft& a = snap.ac[selIdx];
    g.setFont(&FreeSansBold12pt7b);
    g.setTextColor(COL_SEL);
    g.setCursor(PANEL_X + 18, CARD_Y + 26);
    g.print(labelOf(a));
    g.setFont(&FreeSans9pt7b);
    g.setTextColor(COL_TEXT);
    g.setCursor(PANEL_X + 18, CARD_Y + 46);
    const char* airline = airlineName(a.call);
    if (airline) g.print(airline); else g.printf("Hex %s", a.hex);
    g.setCursor(PANEL_X + 18, CARD_Y + 66);
    g.printf("Hoehe %d ft   Speed %d kn", a.alt, a.spd);
    g.setCursor(PANEL_X + 18, CARD_Y + 84);
    g.printf("Kurs %d Grad   Dist. %d km", (int)a.hdg, (int)lroundf(vd[selIdx]));
  } else {
    g.setFont(&FreeSans9pt7b);
    g.setTextColor(COL_DIM);
    g.setCursor(PANEL_X + 18, CARD_Y + 50);
    g.print("Flugzeug antippen");
  }
}

// ---------- Öffentliche Funktionen ----------
void guiBegin(Lcd7& l) {
  lcd = &l;
  lcd->fillRect(0, 0, LCD_W, LCD_H, COL_BG);
  panelDirty = true;
}

void guiNewData(const AircraftSnapshot& s) {
  snap = s;
  panelDirty = true;
}

static void selectAircraft(int i) {
  if (i < 0 || i == selIdx) {          // erneutes Tippen hebt die Auswahl auf
    selHex[0] = 0;
    selIdx = -1;
  } else {
    strlcpy(selHex, snap.ac[i].hex, sizeof(selHex));
    selIdx = i;
  }
  panelDirty = true;
}

void guiTap(int x, int y) {
  // Radius-Tasten
  for (int i = 0; i < 4; i++) {
    const int bx = PANEL_X + 8 + i * BTN_STEP;
    if (x >= bx && x < bx + BTN_W && y >= BTN_Y && y < BTN_Y + BTN_H) {
      rangeIdx = i;
      panelDirty = true;
      Serial.printf("Radius: %d km\n", (int)RANGES[i]);
      return;
    }
  }
  // Liste
  for (int r = 0; r < snap.count && r < ROW_COUNT; r++) {
    const int ry = ROW_Y + r * ROW_STEP;
    if (x >= PANEL_X + 8 && x < PANEL_X + PANEL_W - 8 && y >= ry && y < ry + ROW_H) {
      selectAircraft(order[r]);
      return;
    }
  }
  // Flugzeug im Radar
  for (int i = 0; i < snap.count; i++) {
    int px, py;
    planePos(i, px, py);
    const int dx = x - px, dy = y - py;
    if (dx * dx + dy * dy < 35 * 35) {
      selectAircraft(i);
      return;
    }
  }
}

void guiRender() {
  updateView();
  lcd->render(0, 0, RADAR_W, LCD_H, COL_BG, radarScene);

  if (panelDirty || millis() - lastPanel > 1000) {   // Leiste nur bei Bedarf neu zeichnen
    lcd->render(PANEL_X, 0, PANEL_W, LCD_H, COL_PANEL, panelScene);
    panelDirty = false;
    lastPanel = millis();
  }
}
