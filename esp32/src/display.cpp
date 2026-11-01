#include "display.h"
#include "geo.h"
#include <U8g2lib.h>
#include <Wire.h>
#include <math.h>

// Bei verschobenem Bild stattdessen SH1106 verwenden
static U8G2_SSD1306_128X64_NONAME_F_HW_I2C oled(U8G2_R0, U8X8_PIN_NONE);

void displayInit() {
  oled.begin();
}

void displayDrawRadar(const Aircraft* ac, int count) {
  const int cx = 32, cy = 32, r = 31;
  oled.clearBuffer();
  oled.drawCircle(cx, cy, r);
  oled.drawCircle(cx, cy, r / 2);
  oled.drawPixel(cx, cy);

  for (int i = 0; i < count; i++) {
    int px, py;
    toRadar(ac[i].lat, ac[i].lon, cx, cy, r, px, py);
    if ((px - cx) * (px - cx) + (py - cy) * (py - cy) > r * r) continue;
    oled.drawDisc(px, py, 2);
    float h = ac[i].hdg * DEG_TO_RAD;  // kurzer Strich in Flugrichtung
    oled.drawLine(px, py, px + (int)(5 * sinf(h)), py - (int)(5 * cosf(h)));
  }

  oled.setFont(u8g2_font_5x7_tf);
  oled.setCursor(68, 8);
  oled.printf("AC: %d", count);
  for (int i = 0; i < count && i < 5; i++) {
    oled.setCursor(68, 18 + i * 9);
    oled.printf("%.6s", ac[i].call);
  }
  oled.sendBuffer();
}

void displayShowError(const char* msg) {
  oled.clearBuffer();
  oled.setFont(u8g2_font_5x7_tf);
  oled.setCursor(0, 10);
  oled.print(msg);
  oled.sendBuffer();
}
