#include "touch.h"
#include <SPI.h>
#include <Preferences.h>
#include <math.h>
#include <Fonts/FreeSans9pt7b.h>
#include <Fonts/FreeSansBold12pt7b.h>

static const uint8_t T_CLK  = 12;
static const uint8_t T_MISO = 13;
static const uint8_t T_MOSI = 11;
static const uint8_t T_CS   = 10;
static const int     MARGIN = 35;

static SPIClass    touchSPI(HSPI);
static Preferences prefs;

// Affine Kalibrierparameter:
// screenX = calA * rawX + calB * rawY + calC
// screenY = calD * rawX + calE * rawY + calF
static float calA = (LCD_W - 2.0f * MARGIN) / 3600.0f;
static float calB = 0.0f;
static float calC = MARGIN - calA * 300.0f;
static float calD = 0.0f;
static float calE = (LCD_H - 2.0f * MARGIN) / 3500.0f;
static float calF = MARGIN - calE * 200.0f;

static void sortUint16(uint16_t* arr, int n) {
  for (int i = 1; i < n; i++) {
    uint16_t key = arr[i];
    int j = i - 1;
    while (j >= 0 && arr[j] > key) {
      arr[j + 1] = arr[j];
      j--;
    }
    arr[j + 1] = key;
  }
}

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
  int16_t z = (int16_t)z1 + 4095 - (int16_t)z2;

  // Höhere Schwelle verhindert Fehlauslösungen beim leichten Aufsetzen/Abheben
  if (z < 750 || z1 < 80 || z2 > 4000) {
    readXPT2046(0xD0);
    digitalWrite(T_CS, HIGH);
    touchSPI.endTransaction();
    return false;
  }

  // Dummy-Read nach Achsenumschaltung, damit die Kapazität des 7"-Panels einschwingt
  readXPT2046(0xD1);
  uint16_t xs[7], ys[7];
  for (int i = 0; i < 7; i++) {
    xs[i] = readXPT2046(0xD1);
  }

  readXPT2046(0x91);
  for (int i = 0; i < 7; i++) {
    ys[i] = readXPT2046(i == 6 ? 0x90 : 0x91);
  }
  readXPT2046(0xD0);   // PENIRQ wieder aktivieren

  digitalWrite(T_CS, HIGH);
  touchSPI.endTransaction();

  sortUint16(xs, 7);
  sortUint16(ys, 7);

  // Stark schwankende Messreihe (Finger prellt oder hebt gerade ab) verwerfen
  if ((xs[5] - xs[1]) > 90 || (ys[5] - ys[1]) > 90) {
    return false;
  }

  rawX = xs[3];   // Median
  rawY = ys[3];   // Median
  return (rawX >= 100 && rawX <= 4000 && rawY >= 100 && rawY <= 4000);
}

// Präzises Fadenkreuz mit 1-px-Fadenlinie in der Mitte zum exakten Anvisieren
static void drawCrosshair(Lcd7& lcd, int cx, int cy, uint16_t color) {
  // Äußere dicke Balken (mit kleiner Lücke um das Zentrum)
  lcd.fillRect(cx - 24, cy - 1, 18, 3, color);
  lcd.fillRect(cx + 7,  cy - 1, 18, 3, color);
  lcd.fillRect(cx - 1, cy - 24, 3, 18, color);
  lcd.fillRect(cx - 1, cy + 7,  3, 18, color);
  // Feines 1-px-Kreuz exakt durch den Zielpunkt
  lcd.fillRect(cx - 6, cy, 13, 1, color);
  lcd.fillRect(cx, cy - 6, 1, 13, color);
}

static char calLine1[48] = "";
static char calLine2[64] = "";

static void calInfoScene(Stripe& g) {
  g.setFont(&FreeSansBold12pt7b);
  g.setTextColor(0xFFFF);
  int16_t bx, by; uint16_t bw, bh;
  g.getTextBounds(calLine1, 0, 0, &bx, &by, &bw, &bh);
  g.setCursor(400 - (int)bw / 2 - bx, 215);
  g.print(calLine1);

  g.setFont(&FreeSans9pt7b);
  g.setTextColor(rgb565(170, 195, 220));
  g.getTextBounds(calLine2, 0, 0, &bx, &by, &bw, &bh);
  g.setCursor(400 - (int)bw / 2 - bx, 248);
  g.print(calLine2);
}

static void showCalInfo(Lcd7& lcd, const char* l1, const char* l2) {
  snprintf(calLine1, sizeof(calLine1), "%s", l1);
  snprintf(calLine2, sizeof(calLine2), "%s", l2);
  lcd.render(120, 185, 560, 80, 0x0000, calInfoScene);
}

void touchCalibrate(Lcd7& lcd) {
  // 5 Kalibrierpunkte: 4 Ecken + exakte Bildschirmmitte
  const int16_t ptX[5] = { MARGIN, LCD_W - MARGIN, LCD_W - MARGIN, MARGIN, LCD_W / 2 };
  const int16_t ptY[5] = { MARGIN, MARGIN, LCD_H - MARGIN, LCD_H - MARGIN, LCD_H / 2 };
  uint16_t calX[5], calY[5];
  uint16_t rx, ry;

  // Warten, falls der Finger noch vom Button auf dem Display liegt
  while (readRawTouch(rx, ry)) delay(20);
  delay(200);

  while (true) {
    lcd.fillRect(0, 0, LCD_W, LCD_H, 0x0000);
    Serial.println("5-Punkt-Kalibrierung gestartet.");

    for (int i = 0; i < 5; i++) {
      // Für Punkt 5 (Bildschirmmitte) vorher den mittleren Infotext löschen
      if (i == 4) {
        lcd.fillRect(120, 185, 560, 80, 0x0000);
      } else {
        char buf[48];
        snprintf(buf, sizeof(buf), "Punkt %d von 5 exakt antippen", i + 1);
        showCalInfo(lcd, "Touch-Kalibrierung", buf);
      }

      drawCrosshair(lcd, ptX[i], ptY[i], 0xFFFF);

      uint16_t bufX[25], bufY[25];
      uint8_t valid = 0;

      while (true) {
        while (!readRawTouch(rx, ry)) delay(10);
        delay(120);   // Einschwingzeit abwarten, bis der Finger ruhig aufliegt

        valid = 0;
        for (int s = 0; s < 25; s++) {
          if (readRawTouch(rx, ry)) {
            bufX[valid] = rx;
            bufY[valid] = ry;
            valid++;
          }
          delay(8);
        }

        if (valid >= 12) break;   // Genug stabile Messwerte gesammelt

        // Zu kurz/wackelig angetippt -> rot blinken und Punkt wiederholen
        drawCrosshair(lcd, ptX[i], ptY[i], 0xF800);
        while (readRawTouch(rx, ry)) delay(15);
        delay(200);
        drawCrosshair(lcd, ptX[i], ptY[i], 0xFFFF);
      }

      sortUint16(bufX, valid);
      sortUint16(bufY, valid);
      calX[i] = bufX[valid / 2];   // Median aller 25 Messungen
      calY[i] = bufY[valid / 2];

      drawCrosshair(lcd, ptX[i], ptY[i], 0x07E0);   // Grün = erfasst
      Serial.printf("Punkt %d (%d,%d): rawX=%d, rawY=%d\n", i + 1, ptX[i], ptY[i], calX[i], calY[i]);

      while (readRawTouch(rx, ry)) delay(15);       // Warten, bis der Finger weg ist
      delay(250);
      drawCrosshair(lcd, ptX[i], ptY[i], 0x0000);
    }

    // 2D-Affin-Transformation per Kleinste-Quadrate-Schätzung berechnen
    float meanU = 0, meanV = 0, meanX = 0, meanY = 0;
    for (int i = 0; i < 5; i++) {
      meanU += calX[i];
      meanV += calY[i];
      meanX += ptX[i];
      meanY += ptY[i];
    }
    meanU /= 5.0f; meanV /= 5.0f;
    meanX /= 5.0f; meanY /= 5.0f;

    float suu = 0, svv = 0, suv = 0;
    float sux = 0, svx = 0, suy = 0, svy = 0;
    for (int i = 0; i < 5; i++) {
      float du = calX[i] - meanU;
      float dv = calY[i] - meanV;
      float dx = ptX[i] - meanX;
      float dy = ptY[i] - meanY;
      suu += du * du;
      svv += dv * dv;
      suv += du * dv;
      sux += du * dx;
      svx += dv * dx;
      suy += du * dy;
      svy += dv * dy;
    }

    float det = suu * svv - suv * suv;
    if (fabsf(det) < 1e9f) {
      showCalInfo(lcd, "Messung ungueltig", "Bitte alle 5 Kreuze einzeln antippen");
      delay(1500);
      continue;
    }

    float na = (svv * sux - suv * svx) / det;
    float nb = (suu * svx - suv * sux) / det;
    float nc = meanX - na * meanU - nb * meanV;

    float nd = (svv * suy - suv * svy) / det;
    float ne = (suu * svy - suv * suy) / det;
    float nf = meanY - nd * meanU - ne * meanV;

    // Restfehler an allen 5 Punkten in Pixeln überprüfen
    float maxErr = 0.0f;
    for (int i = 0; i < 5; i++) {
      float px = na * calX[i] + nb * calY[i] + nc;
      float py = nd * calX[i] + ne * calY[i] + nf;
      float err = hypotf(px - ptX[i], py - ptY[i]);
      if (err > maxErr) maxErr = err;
    }

    if (maxErr > 25.0f) {
      char errMsg[64];
      snprintf(errMsg, sizeof(errMsg), "Abweichung %.0f px - bitte wiederholen", maxErr);
      showCalInfo(lcd, "Ungenaue Messung", errMsg);
      Serial.println(errMsg);
      delay(1600);
      continue;
    }

    calA = na; calB = nb; calC = nc;
    calD = nd; calE = ne; calF = nf;

    prefs.putFloat("a", calA);
    prefs.putFloat("b", calB);
    prefs.putFloat("c", calC);
    prefs.putFloat("d", calD);
    prefs.putFloat("e", calE);
    prefs.putFloat("f", calF);
    prefs.putBool("ok5", true);

    char okMsg[64];
    snprintf(okMsg, sizeof(okMsg), "Max. Abweichung: %.1f px (gespeichert)", maxErr);
    showCalInfo(lcd, "Kalibrierung erfolgreich!", okMsg);
    Serial.printf("Kalibrierung gespeichert. %s\n", okMsg);
    delay(1100);
    break;
  }
}

void touchBegin(Lcd7& lcd) {
  pinMode(T_CS, OUTPUT);
  digitalWrite(T_CS, HIGH);
  touchSPI.begin(T_CLK, T_MISO, T_MOSI, T_CS);
  prefs.begin("touch", false);

  const bool saved = prefs.getBool("ok5", false);

  // Notfall-Fallback beim Start (falls Touch komplett verstellt sein sollte)
  lcd.fillRect(LCD_W / 2 - 40, LCD_H / 2 - 40, 80, 80, 0xF800);
  Serial.println("Zum Neu-Kalibrieren in den naechsten 2 Sekunden den Bildschirm beruehren.");
  bool force = false;
  uint16_t rx, ry;
  const uint32_t t0 = millis();
  while (millis() - t0 < 2000) {
    if (readRawTouch(rx, ry)) { force = true; break; }
    delay(20);
  }
  lcd.fillRect(LCD_W / 2 - 40, LCD_H / 2 - 40, 80, 80, 0x0000);

  if (!saved || force) {
    touchCalibrate(lcd);
  } else {
    calA = prefs.getFloat("a", calA);
    calB = prefs.getFloat("b", calB);
    calC = prefs.getFloat("c", calC);
    calD = prefs.getFloat("d", calD);
    calE = prefs.getFloat("e", calE);
    calF = prefs.getFloat("f", calF);
    Serial.println("Gespeicherte 5-Punkt-Kalibrierung geladen.");
  }
}

bool touchGet(int16_t& x, int16_t& y) {
  uint16_t rawX, rawY;
  if (!readRawTouch(rawX, rawY)) return false;

  const float fx = calA * rawX + calB * rawY + calC;
  const float fy = calD * rawX + calE * rawY + calF;

  x = constrain((int16_t)lroundf(fx), 0, LCD_W - 1);
  y = constrain((int16_t)lroundf(fy), 0, LCD_H - 1);
  return true;
}