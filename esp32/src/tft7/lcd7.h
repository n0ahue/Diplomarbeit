#pragma once
#include <Arduino.h>
#include <Adafruit_GFX.h>

// 7"-Display mit SSD1963-Controller, 16-Bit-Parallelbus (8080-Interface).
//
// Es gibt keinen Bildspeicher im ESP32 (800x480x2 Byte = 768 KB wären zu viel).
// Stattdessen wird in Streifen gezeichnet: Ein Streifen ist STRIPE_H Zeilen hoch,
// wird im RAM mit Adafruit_GFX gezeichnet und dann schnell zum Display geschickt.

constexpr int LCD_W    = 800;
constexpr int LCD_H    = 480;
constexpr int STRIPE_H = 16;

// Bildausrichtung (SSD1963-Befehl 0x36, "Set Address Mode"):
//   0x00 = normal            0x01 = vertikal gespiegelt
//   0x02 = horizontal gespiegelt   0x03 = beide Richtungen gespiegelt (= um 180 Grad gedreht)
// Steht das Bild auf dem Kopf oder seitenverkehrt, hier den passenden Wert eintragen.
// Nach einer Änderung den Touch neu kalibrieren (rotes Quadrat beim Start berühren).
constexpr uint8_t LCD_ADDRESS_MODE = 0x01;

constexpr uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
  return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

// Zeichenfläche für einen Streifen. Alle Koordinaten sind Bildschirmkoordinaten,
// Zeichnen außerhalb des aktuellen Streifens wird einfach abgeschnitten.
class Stripe : public Adafruit_GFX {
 public:
  explicit Stripe(uint16_t* buffer) : Adafruit_GFX(LCD_W, LCD_H), buf_(buffer) {}

  void setStripe(int x, int w, int y, int h, uint16_t bg);
  // true, wenn der Bereich y..y+h den aktuellen Streifen berührt (zum Überspringen unnötiger Arbeit)
  bool overlaps(int y, int h) const { return y < sy_ + sh_ && y + h > sy_; }

  void drawPixel(int16_t x, int16_t y, uint16_t color) override;
  void writeFillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) override;
  void writeFastHLine(int16_t x, int16_t y, int16_t w, uint16_t color) override;
  void writeFastVLine(int16_t x, int16_t y, int16_t h, uint16_t color) override;

 private:
  uint16_t* buf_;
  int rx_ = 0, rw_ = 0, sy_ = 0, sh_ = 0;   // Region-X, Region-Breite, Streifen-Y, Streifenhöhe
};

class Lcd7 {
 public:
  using SceneFn = void (*)(Stripe& g);

  void begin();

  // Gefülltes Rechteck direkt zum Display (sehr schnell, ohne Streifen)
  void fillRect(int x, int y, int w, int h, uint16_t color);

  // Zeichnet den Bereich x,y,w,h: Für jeden Streifen wird mit "bg" gefüllt, "scene" aufgerufen
  // und das Ergebnis zum Display geschickt. "scene" wird mehrmals mit verschiedenen Streifen
  // aufgerufen und muss deshalb jedes Mal exakt dasselbe zeichnen.
  void render(int x, int y, int w, int h, uint16_t bg, SceneFn scene);

 private:
  void initController();
  void setWindow(int x, int y, int w, int h);
  void pushPixels(const uint16_t* px, uint32_t n);
};
