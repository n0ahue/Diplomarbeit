#include "touch.h"
#include <SPI.h>
#include <Preferences.h>

static const uint8_t T_CLK  = 12;
static const uint8_t T_MISO = 13;
static const uint8_t T_MOSI = 11;
static const uint8_t T_CS   = 10;
static const int     MARGIN = 35;

static SPIClass    touchSPI(HSPI);
static Preferences prefs;

static bool    swapXY  = false;
static int32_t hLeft   = 300;
static int32_t hRight  = 3900;
static int32_t vTop    = 200;
static int32_t vBottom = 3700;

static uint16_t readXPT2046(uint8_t cmd) {
  touchSPI.transfer(cmd);
  uint8_t hi = touchSPI.transfer(0x00);
  uint8_t lo = touchSPI.transfer(0x00);
  return ((hi << 8) | lo) >> 3;
}

static bool readRawTouch(uint16_t& rawX, uint16_t& rawY) {
  touchSPI.beginTransaction(SPISettings(1000000, MSBFIRST, SPI_MODE0));
  digitalWrite(T_CS, LOW);

  uint16_t z1 = readXPT2046(0xB1);
  uint16_t z2 = readXPT2046(0xC1);
  int16_t z = z1 + 4095 - z2;

  if (z < 400) {
    readXPT2046(0xD0);
    digitalWrite(T_CS, HIGH);
    touchSPI.endTransaction();
    return false;
  }

  rawX = readXPT2046(0xD1);
  rawY = readXPT2046(0x90);
  readXPT2046(0xD0);

  digitalWrite(T_CS, HIGH);
  touchSPI.endTransaction();

  return (rawX >= 100 && rawX <= 4000 && rawY >= 100 && rawY <= 4000);
}

static void drawCrosshair(Lcd7& lcd, int cx, int cy, uint16_t color) {
  lcd.fillRect(cx - 22, cy - 2, 45, 5, color);
  lcd.fillRect(cx - 2, cy - 22, 5, 45, color);
}

static void calibrate(Lcd7& lcd) {
  const int16_t ptX[4] = { MARGIN, LCD_W - MARGIN, MARGIN, LCD_W - MARGIN };
  const int16_t ptY[4] = { MARGIN, MARGIN, LCD_H - MARGIN, LCD_H - MARGIN };
  uint16_t calX[4], calY[4];

  while (true) {
    lcd.fillRect(0, 0, LCD_W, LCD_H, 0x0000);
    Serial.println("Kalibrierung: Bitte die 4 weissen Fadenkreuze nacheinander antippen.");

    for (int i = 0; i < 4; i++) {
      drawCrosshair(lcd, ptX[i], ptY[i], 0xFFFF);

      uint16_t rx, ry;
      while (!readRawTouch(rx, ry)) delay(15);
      delay(40);   // Einschwingzeit der Touch-Folie

      uint32_t sumX = 0, sumY = 0;
      uint8_t valid = 0;
      for (int s = 0; s < 8; s++) {   // mehrere Messwerte mitteln
        if (readRawTouch(rx, ry)) { sumX += rx; sumY += ry; valid++; }
        delay(10);
      }
      calX[i] = valid ? (sumX / valid) : rx;
      calY[i] = valid ? (sumY / valid) : ry;

      drawCrosshair(lcd, ptX[i], ptY[i], 0x07E0);   // grün = erfasst
      Serial.printf("Ecke %d: rawX=%d, rawY=%d\n", i + 1, calX[i], calY[i]);

      while (readRawTouch(rx, ry)) delay(20);        // warten, bis der Finger weg ist
      delay(250);
      drawCrosshair(lcd, ptX[i], ptY[i], 0x0000);
    }

    swapXY = abs((int32_t)calY[1] - (int32_t)calY[0]) > abs((int32_t)calX[1] - (int32_t)calX[0]);
    if (!swapXY) {
      hLeft = (calX[0] + calX[2]) / 2;  hRight  = (calX[1] + calX[3]) / 2;
      vTop  = (calY[0] + calY[1]) / 2;  vBottom = (calY[2] + calY[3]) / 2;
    } else {
      hLeft = (calY[0] + calY[2]) / 2;  hRight  = (calY[1] + calY[3]) / 2;
      vTop  = (calX[0] + calX[1]) / 2;  vBottom = (calX[2] + calX[3]) / 2;
    }

    if (abs(hRight - hLeft) > 500 && abs(vBottom - vTop) > 500) break;   // Plausibilitätsprüfung
    Serial.println("Ungültige Messung, Kalibrierung wird wiederholt.");
  }

  prefs.putBool("swap", swapXY);
  prefs.putInt("hl", hLeft);
  prefs.putInt("hr", hRight);
  prefs.putInt("vt", vTop);
  prefs.putInt("vb", vBottom);
  prefs.putBool("ok", true);
  Serial.println("Kalibrierung gespeichert.");
}

void touchBegin(Lcd7& lcd) {
  pinMode(T_CS, OUTPUT);
  digitalWrite(T_CS, HIGH);
  touchSPI.begin(T_CLK, T_MISO, T_MOSI, T_CS);
  prefs.begin("touch", false);

  const bool saved = prefs.getBool("ok", false);

  // Rotes Quadrat: Berühren innerhalb von 2 Sekunden startet die Kalibrierung neu
  lcd.fillRect(LCD_W / 2 - 40, LCD_H / 2 - 40, 80, 80, 0xF800);
  Serial.println("Zum Neu-Kalibrieren in den nächsten 2 Sekunden den Bildschirm berühren.");
  bool force = false;
  uint16_t rx, ry;
  const uint32_t t0 = millis();
  while (millis() - t0 < 2000) {
    if (readRawTouch(rx, ry)) { force = true; break; }
    delay(20);
  }
  lcd.fillRect(LCD_W / 2 - 40, LCD_H / 2 - 40, 80, 80, 0x0000);

  if (!saved || force) {
    calibrate(lcd);
  } else {
    swapXY  = prefs.getBool("swap", false);
    hLeft   = prefs.getInt("hl", hLeft);
    hRight  = prefs.getInt("hr", hRight);
    vTop    = prefs.getInt("vt", vTop);
    vBottom = prefs.getInt("vb", vBottom);
    Serial.println("Gespeicherte Kalibrierung geladen.");
  }
}

bool touchGet(int16_t& x, int16_t& y) {
  uint16_t rawX, rawY;
  if (!readRawTouch(rawX, rawY)) return false;

  const int32_t hVal = swapXY ? rawY : rawX;
  const int32_t vVal = swapXY ? rawX : rawY;

  x = constrain(map(hVal, hLeft, hRight, MARGIN, LCD_W - MARGIN), 0, LCD_W - 1);
  y = constrain(map(vVal, vTop, vBottom, MARGIN, LCD_H - MARGIN), 0, LCD_H - 1);
  return true;
}
