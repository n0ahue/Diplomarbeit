// 7"-Flugradar-Oberfläche (800x480):
//  - links das Radar (480x480) mit mehrfarbiger Höhen-Codierung, Kursvektoren & Kompass
//  - rechts die Bedienleiste (320x480) mit Radius-Wahl, Flugzeugliste, Detailkarte & Kalibrierungs-Button

#include <Arduino.h>
#include <math.h>
#include <Fonts/FreeSans9pt7b.h>
#include <Fonts/FreeSansBold12pt7b.h>
#include "lcd7.h"
#include "touch.h"

// ---------- Farben (abgestimmt auf hohen Kontrast) ----------
constexpr uint16_t COL_BG        = rgb565(0, 0, 0);        // Tiefschwarz
constexpr uint16_t COL_RADAR_BG  = rgb565(0, 0, 40);       // Sehr dunkles Nachtblau
constexpr uint16_t COL_RING      = rgb565(0, 150, 220);    // Helles Cyan
constexpr uint16_t COL_RING2     = rgb565(0, 0, 110);      // Dunkelblau
constexpr uint16_t COL_TEXT      = rgb565(255, 255, 255);  // Strahlendes Eisweiß
constexpr uint16_t COL_DIM       = rgb565(0, 140, 210);    // Gedimmtes Cyan
constexpr uint16_t COL_PANEL     = rgb565(0, 0, 40);       // Dunkles Nachtblau
constexpr uint16_t COL_ROW       = rgb565(0, 0, 100);      // Dunkles Königsblau
constexpr uint16_t COL_ROW_SEL   = rgb565(0, 0, 165);      // Kräftiges Blau
constexpr uint16_t COL_SEL       = rgb565(255, 255, 255);  // Weißer Auswahlrahmen
constexpr uint16_t COL_BTN       = rgb565(0, 0, 100);      // Dunkelblau
constexpr uint16_t COL_ACCENT    = rgb565(0, 220, 255);    // Leuchtendes Cyan
constexpr uint16_t COL_OK        = rgb565(0, 255, 0);      // Sattes Grün

// 5 kontrastreiche Höhenstufen
constexpr uint16_t COL_ALT_LOW   = rgb565(255, 255, 255);  // < 5.000 ft: Eisweiß (An-/Abflug)
constexpr uint16_t COL_ALT_MID1  = rgb565(0, 255, 110);    // 5.000..14.999 ft: Helles Mintgrün
constexpr uint16_t COL_ALT_MID2  = rgb565(0, 255, 0);      // 15.000..24.999 ft: Smaragdgrün
constexpr uint16_t COL_ALT_HIGH  = rgb565(0, 220, 255);    // 25.000..34.999 ft: Cyan
constexpr uint16_t COL_ALT_CRZ   = rgb565(0, 110, 255);    // >= 35.000 ft: Himmelblau

// ---------- Layout ----------
constexpr int RADAR_W = 480;
constexpr int RADAR_CX = 240, RADAR_CY = 240, RADAR_R = 225;
constexpr int PANEL_X = 480, PANEL_W = 320;
constexpr int CAL_BTN_X = PANEL_X + 168, CAL_BTN_Y = 8, CAL_BTN_W = 96, CAL_BTN_H = 32;
constexpr int BTN_Y = 52, BTN_H = 44, BTN_W = 72, BTN_STEP = 78;
constexpr int ROW_Y = 112, ROW_H = 48, ROW_STEP = 52;

// ---------- Simulierte Flugzeuge ----------
struct Plane {
  const char* call;
  const char* airline;
  int   alt;
  int   spd;
  float hdg;
  float x0, y0;
  float x, y;
};

static Plane planes[] = {
  {"AUA123", "Austrian Airlines", 36000, 450,  90.0f, -60.0f,  20.0f},
  {"DLH4AB", "Lufthansa",         28000, 420, 200.0f,  20.0f,  60.0f},
  {"RYR7XZ", "Ryanair",           18000, 310, 330.0f,  30.0f, -50.0f},
  {"EZY81B", "easyJet",            8500, 210,  45.0f, -70.0f, -60.0f},
  {"SWR19K", "Swiss",              3800, 165, 270.0f,  75.0f, -15.0f},
};
constexpr int NPLANES = sizeof(planes) / sizeof(planes[0]);
constexpr float SIM_SPEEDUP = 10.0f;
constexpr float SIM_AREA_KM = 150.0f;

static const float RANGES[4] = {25.0f, 50.0f, 100.0f, 150.0f};

static Lcd7  lcd;
static float rangeKm  = 100.0f;
static int   selected = -1;
static bool  panelDirty = true;
static int   order[NPLANES];
static float distKm[NPLANES];

static float wrapKm(float v) {
  float w = fmodf(v + SIM_AREA_KM, 2.0f * SIM_AREA_KM);
  if (w < 0) w += 2.0f * SIM_AREA_KM;
  return w - SIM_AREA_KM;
}

static void updatePlanes() {
  const float t = (millis() / 1000.0f) * SIM_SPEEDUP;
  for (int i = 0; i < NPLANES; i++) {
    Plane& p = planes[i];
    const float d = p.spd * 0.000514444f * t;
    const float h = p.hdg * DEG_TO_RAD;
    p.x = wrapKm(p.x0 + sinf(h) * d);
    p.y = wrapKm(p.y0 + cosf(h) * d);
    distKm[i] = sqrtf(p.x * p.x + p.y * p.y);
  }
  for (int i = 0; i < NPLANES; i++) order[i] = i;
  for (int i = 1; i < NPLANES; i++) {
    int k = order[i], j = i - 1;
    while (j >= 0 && distKm[order[j]] > distKm[k]) { order[j + 1] = order[j]; j--; }
    order[j + 1] = k;
  }
}

static uint16_t altColor(int altFt) {
  if (altFt < 5000)  return COL_ALT_LOW;
  if (altFt < 15000) return COL_ALT_MID1;
  if (altFt < 25000) return COL_ALT_MID2;
  if (altFt < 35000) return COL_ALT_HIGH;
  return COL_ALT_CRZ;
}

static void planePos(const Plane& p, int& px, int& py) {
  px = RADAR_CX + (int)lroundf(p.x * RADAR_R / rangeKm);
  py = RADAR_CY - (int)lroundf(p.y * RADAR_R / rangeKm);
}

// ---------- Zeichnen ----------
static void rotPoint(float lx, float ly, float c, float s, int cx, int cy, int16_t& ox, int16_t& oy) {
  ox = cx + (int16_t)lroundf(lx * c - ly * s);
  oy = cy + (int16_t)lroundf(lx * s + ly * c);
}

static void drawPlane(Stripe& g, int cx, int cy, float hdgDeg, uint16_t col) {
  const float h = hdgDeg * DEG_TO_RAD;
  const float c = cosf(h), s = sinf(h);

  int16_t vx, vy;
  rotPoint(0, -34, c, s, cx, cy, vx, vy);
  g.drawLine(cx, cy, vx, vy, col);

  static const float T[][6] = {
    {0, -13,  3,   7, -3,   7},
    {0,  -3, 13,   5,  0,   2},
    {0,  -3, -13,  5,  0,   2},
    {0,   4,  6,   9,  0,   7},
    {0,   4, -6,   9,  0,   7},
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
  g.fillCircle(RADAR_CX, RADAR_CY, RADAR_R, COL_RADAR_BG);

  for (int i = 1; i <= 4; i++) {
    g.drawCircle(RADAR_CX, RADAR_CY, RADAR_R * i / 4, i == 4 ? COL_RING : COL_RING2);
  }
  g.drawFastHLine(RADAR_CX - RADAR_R, RADAR_CY, 2 * RADAR_R, COL_RING2);
  g.drawFastVLine(RADAR_CX, RADAR_CY - RADAR_R, 2 * RADAR_R, COL_RING2);

  g.setFont(&FreeSans9pt7b);
  g.setTextColor(COL_DIM);
  for (int i = 1; i <= 3; i++) {
    const int y = RADAR_CY - RADAR_R * i / 4;
    if (!g.overlaps(y - 18, 22)) continue;
    g.setCursor(RADAR_CX + 5, y - 3);
    g.printf("%d", (int)(rangeKm * i / 4));
  }

  g.setFont(&FreeSansBold12pt7b);
  g.setTextColor(COL_ACCENT);
  if (g.overlaps(RADAR_CY - RADAR_R + 4, 24)) {
    g.setCursor(RADAR_CX - 8, RADAR_CY - RADAR_R + 24);
    g.print("N");
  }
  g.setFont(&FreeSans9pt7b);
  g.setTextColor(COL_DIM);
  if (g.overlaps(RADAR_CY + RADAR_R - 22, 20)) {
    g.setCursor(RADAR_CX - 6, RADAR_CY + RADAR_R - 8);
    g.print("S");
  }
  if (g.overlaps(RADAR_CY - 8, 18)) {
    g.setCursor(RADAR_CX + RADAR_R - 18, RADAR_CY + 5);
    g.print("O");
    g.setCursor(RADAR_CX - RADAR_R + 6, RADAR_CY + 5);
    g.print("W");
  }

  g.fillCircle(RADAR_CX, RADAR_CY, 4, COL_TEXT);
  g.drawCircle(RADAR_CX, RADAR_CY, 7, COL_ACCENT);

  g.setFont(&FreeSans9pt7b);
  for (int i = 0; i < NPLANES; i++) {
    const Plane& p = planes[i];
    int px, py;
    planePos(p, px, py);
    const int dx = px - RADAR_CX, dy = py - RADAR_CY;
    if (dx * dx + dy * dy > RADAR_R * RADAR_R) continue;
    if (!g.overlaps(py - 40, 80)) continue;

    const uint16_t col = altColor(p.alt);
    drawPlane(g, px, py, p.hdg, col);

    g.setTextColor(i == selected ? COL_SEL : col);
    g.setCursor(px + 16, py + 5);
    g.print(p.call);

    if (i == selected) {
      g.drawCircle(px, py, 20, COL_SEL);
      g.drawCircle(px, py, 21, COL_SEL);
    }
  }
}

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
  g.drawFastVLine(PANEL_X, 0, LCD_H, COL_RING);

  g.setFont(&FreeSansBold12pt7b);
  g.setTextColor(COL_TEXT);
  g.setCursor(PANEL_X + 12, 32);
  g.print("Flugradar");

  g.fillRoundRect(CAL_BTN_X, CAL_BTN_Y, CAL_BTN_W, CAL_BTN_H, 6, COL_BTN);
  g.drawRoundRect(CAL_BTN_X, CAL_BTN_Y, CAL_BTN_W, CAL_BTN_H, 6, COL_RING);
  g.setFont(&FreeSans9pt7b);
  g.setTextColor(COL_TEXT);
  printCentered(g, CAL_BTN_X + CAL_BTN_W / 2, CAL_BTN_Y + CAL_BTN_H / 2 + 5, "Kalib.");

  g.fillCircle(PANEL_X + PANEL_W - 20, 22, 7, COL_OK);

  g.setFont(&FreeSans9pt7b);
  for (int i = 0; i < 4; i++) {
    const int x = PANEL_X + 8 + i * BTN_STEP;
    const bool active = (RANGES[i] == rangeKm);
    g.fillRoundRect(x, BTN_Y, BTN_W, BTN_H, 8, active ? COL_ACCENT : COL_BTN);
    if (!active) g.drawRoundRect(x, BTN_Y, BTN_W, BTN_H, 8, COL_RING);
    char buf[12];
    snprintf(buf, sizeof(buf), "%d km", (int)RANGES[i]);
    g.setTextColor(active ? COL_BG : COL_TEXT);
    printCentered(g, x + BTN_W / 2, BTN_Y + BTN_H / 2 + 6, buf);
  }

  g.setTextColor(COL_DIM);
  g.setCursor(PANEL_X + 12, ROW_Y - 8);
  g.printf("Naechste Flugzeuge (%d)", NPLANES);

  for (int r = 0; r < NPLANES && r < 5; r++) {
    const Plane& p = planes[order[r]];
    const int y = ROW_Y + r * ROW_STEP;
    const bool sel = (order[r] == selected);
    const uint16_t aCol = altColor(p.alt);

    g.fillRoundRect(PANEL_X + 8, y, PANEL_W - 16, ROW_H, 6, sel ? COL_SEL : COL_ROW);
    if (sel) g.fillRoundRect(PANEL_X + 10, y + 2, PANEL_W - 20, ROW_H - 4, 5, COL_ROW_SEL);

    g.fillRoundRect(PANEL_X + 12, y + 6, 5, ROW_H - 12, 2, aCol);

    g.setFont(&FreeSansBold12pt7b);
    g.setTextColor(aCol);
    g.setCursor(PANEL_X + 24, y + 22);
    g.print(p.call);

    g.setFont(&FreeSans9pt7b);
    g.setTextColor(COL_TEXT);
    g.setCursor(PANEL_X + 24, y + 41);
    g.print(p.airline);

    char fl[12];
    snprintf(fl, sizeof(fl), "FL%03d", p.alt / 100);
    g.setTextColor(COL_TEXT);
    printRight(g, PANEL_X + PANEL_W - 16, y + 22, fl);
    char dd[12];
    snprintf(dd, sizeof(dd), "%d km", (int)lroundf(distKm[order[r]]));
    g.setTextColor(COL_DIM);
    printRight(g, PANEL_X + PANEL_W - 16, y + 41, dd);
  }

  const int cy = 380;
  g.fillRoundRect(PANEL_X + 8, cy, PANEL_W - 16, 92, 8, COL_ROW);
  if (selected >= 0) {
    const Plane& p = planes[selected];
    const uint16_t aCol = altColor(p.alt);
    g.drawRoundRect(PANEL_X + 8, cy, PANEL_W - 16, 92, 8, COL_SEL);
    g.fillRoundRect(PANEL_X + 12, cy + 8, 5, 76, 2, aCol);

    g.setFont(&FreeSansBold12pt7b);
    g.setTextColor(COL_SEL);
    g.setCursor(PANEL_X + 24, cy + 26);
    g.print(p.call);

    g.setFont(&FreeSans9pt7b);
    g.setTextColor(aCol);
    printRight(g, PANEL_X + PANEL_W - 16, cy + 24, p.airline);

    g.setTextColor(COL_TEXT);
    g.setCursor(PANEL_X + 24, cy + 52);
    g.printf("Hoehe: %d ft   Speed: %d kn", p.alt, p.spd);
    g.setCursor(PANEL_X + 24, cy + 76);
    g.printf("Kurs: %03d Grad   Dist.: %d km", (int)p.hdg, (int)lroundf(distKm[selected]));
  } else {
    g.drawRoundRect(PANEL_X + 8, cy, PANEL_W - 16, 92, 8, COL_RING);
    g.setFont(&FreeSans9pt7b);
    g.setTextColor(COL_DIM);
    printCentered(g, PANEL_X + PANEL_W / 2, cy + 50, "Flugzeug auf Radar/Liste antippen");
  }
}

// ---------- Touch ----------
static void handleTap(int x, int y) {
  if (x >= CAL_BTN_X && x < CAL_BTN_X + CAL_BTN_W &&
      y >= CAL_BTN_Y && y < CAL_BTN_Y + CAL_BTN_H) {
    Serial.println("Touch-Kalibrierung ueber Display-Button gestartet.");
    touchCalibrate(lcd);
    lcd.fillRect(0, 0, LCD_W, LCD_H, COL_BG);
    panelDirty = true;
    return;
  }

  for (int i = 0; i < 4; i++) {
    const int bx = PANEL_X + 8 + i * BTN_STEP;
    if (x >= bx && x < bx + BTN_W && y >= BTN_Y && y < BTN_Y + BTN_H) {
      rangeKm = RANGES[i];
      panelDirty = true;
      Serial.printf("Radius: %d km\n", (int)rangeKm);
      return;
    }
  }

  for (int r = 0; r < NPLANES && r < 5; r++) {
    const int ry = ROW_Y + r * ROW_STEP;
    if (x >= PANEL_X + 8 && x < PANEL_X + PANEL_W - 8 && y >= ry && y < ry + ROW_H) {
      selected = (selected == order[r]) ? -1 : order[r];
      panelDirty = true;
      return;
    }
  }

  for (int i = 0; i < NPLANES; i++) {
    int px, py;
    planePos(planes[i], px, py);
    const int dx = x - px, dy = y - py;
    if (dx * dx + dy * dy < 35 * 35) {
      selected = (selected == i) ? -1 : i;
      panelDirty = true;
      return;
    }
  }
}

void setup() {
  Serial.begin(115200);
  delay(800);
  Serial.println("\n7\"-Display-Demo gestartet");

  lcd.begin();
  lcd.fillRect(0, 0, LCD_W, LCD_H, COL_BG);
  touchBegin(lcd);

  updatePlanes();

  uint32_t t0 = micros();
  lcd.fillRect(0, 0, LCD_W, LCD_H, COL_BG);
  Serial.printf("Vollbild einfarbig füllen: %lu ms\n", (unsigned long)((micros() - t0) / 1000));
  t0 = micros();
  lcd.render(0, 0, RADAR_W, LCD_H, COL_BG, radarScene);
  Serial.printf("Radar (480x480) zeichnen: %lu ms\n", (unsigned long)((micros() - t0) / 1000));
  t0 = micros();
  lcd.render(PANEL_X, 0, PANEL_W, LCD_H, COL_PANEL, panelScene);
  Serial.printf("Bedienleiste (320x480) zeichnen: %lu ms\n", (unsigned long)((micros() - t0) / 1000));
  panelDirty = false;
}

void loop() {
  static uint32_t frames = 0, radarUs = 0, lastPrint = 0, lastPanel = 0, lastTap = 0;
  static uint8_t  touchStreak = 0;
  static bool     wasTouched = false;

  int16_t tx, ty;
  if (touchGet(tx, ty)) {
    if (touchStreak < 255) touchStreak++;
    if (touchStreak == 2 && !wasTouched && millis() - lastTap > 250) {
      handleTap(tx, ty);
      lastTap = millis();
      wasTouched = true;
    }
  } else {
    touchStreak = 0;
    wasTouched = false;
  }

  updatePlanes();

  const uint32_t t0 = micros();
  lcd.render(0, 0, RADAR_W, LCD_H, COL_BG, radarScene);
  radarUs += micros() - t0;
  frames++;

  if (panelDirty || millis() - lastPanel > 1000) {
    lcd.render(PANEL_X, 0, PANEL_W, LCD_H, COL_PANEL, panelScene);
    panelDirty = false;
    lastPanel = millis();
  }

  if (millis() - lastPrint >= 1000) {
    Serial.printf("Bilder pro Sekunde: %lu, Radar pro Bild: %lu ms\n",
                  (unsigned long)frames, (unsigned long)(radarUs / (frames ? frames : 1) / 1000));
    frames = 0; radarUs = 0; lastPrint = millis();
  }
}