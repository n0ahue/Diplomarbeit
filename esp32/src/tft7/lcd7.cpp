#include "lcd7.h"
#include <initializer_list>
#include "soc/gpio_reg.h"
#include "soc/soc.h"

// ---------- Pinbelegung (aus dem funktionierenden Test-Code) ----------
static const uint8_t PIN_CS  = 14;
static const uint8_t PIN_RS  = 8;    // Command/Data
static const uint8_t PIN_WR  = 9;
static const uint8_t PIN_RST = 21;
static const uint8_t PIN_BL  = 3;    // Hintergrundbeleuchtung
static const uint8_t DB_PINS[16] = {
  1, 2, 42, 41, 40, 39, 38, 47,      // DB0..DB7
  4, 5, 6, 7, 15, 16, 17, 18         // DB8..DB15
};

static const uint32_t WR_MASK = 1u << PIN_WR;
static const uint32_t RS_MASK = 1u << PIN_RS;

// Schnelle Datenausgabe: Für jedes Byte ist vorberechnet, welche GPIOs gesetzt werden müssen.
// GPIO 0..31 liegen im Register "OUT", GPIO 32..48 im Register "OUT1".
static uint32_t lutLo0[256], lutLo1[256], lutHi0[256], lutHi1[256];
static uint32_t dataMask0 = 0, dataMask1 = 0;

static void buildTables() {
  for (int i = 0; i < 16; i++) {
    if (DB_PINS[i] < 32) dataMask0 |= 1u << DB_PINS[i];
    else                 dataMask1 |= 1u << (DB_PINS[i] - 32);
  }
  for (int v = 0; v < 256; v++) {
    lutLo0[v] = lutLo1[v] = lutHi0[v] = lutHi1[v] = 0;
    for (int b = 0; b < 8; b++) {
      if (!((v >> b) & 1)) continue;
      const uint8_t pl = DB_PINS[b];       // Bit 0..7
      const uint8_t ph = DB_PINS[8 + b];   // Bit 8..15
      if (pl < 32) lutLo0[v] |= 1u << pl; else lutLo1[v] |= 1u << (pl - 32);
      if (ph < 32) lutHi0[v] |= 1u << ph; else lutHi1[v] |= 1u << (ph - 32);
    }
  }
}

// Legt 16 Bit auf den Datenbus (WR bleibt unverändert)
static inline __attribute__((always_inline)) void busPut(uint16_t v) {
  const uint32_t s0 = lutLo0[v & 0xFF] | lutHi0[v >> 8];
  const uint32_t s1 = lutLo1[v & 0xFF] | lutHi1[v >> 8];
  REG_WRITE(GPIO_OUT_W1TC_REG,  dataMask0 & ~s0);
  REG_WRITE(GPIO_OUT_W1TS_REG,  s0);
  REG_WRITE(GPIO_OUT1_W1TC_REG, dataMask1 & ~s1);
  REG_WRITE(GPIO_OUT1_W1TS_REG, s1);
}

// WR-Impuls: Das Display übernimmt die Daten bei der steigenden Flanke.
static inline __attribute__((always_inline)) void strobe() {
  REG_WRITE(GPIO_OUT_W1TC_REG, WR_MASK);
  __asm__ __volatile__("nop; nop; nop; nop; nop; nop; nop; nop;");   // WR-Low-Zeit
  REG_WRITE(GPIO_OUT_W1TS_REG, WR_MASK);
}

static inline void writeCmd(uint8_t cmd) {
  REG_WRITE(GPIO_OUT_W1TC_REG, RS_MASK);
  busPut(cmd);
  strobe();
}

static inline void writeData(uint16_t d) {
  REG_WRITE(GPIO_OUT_W1TS_REG, RS_MASK);
  busPut(d);
  strobe();
}

static void cd(uint8_t c, std::initializer_list<uint8_t> params) {
  writeCmd(c);
  for (uint8_t p : params) writeData(p);
}

// ---------- Stripe ----------
static uint16_t stripeBuf[LCD_W * STRIPE_H];
static Stripe   stripe(stripeBuf);

void Stripe::setStripe(int x, int w, int y, int h, uint16_t bg) {
  rx_ = x; rw_ = w; sy_ = y; sh_ = h;
  const int n = w * h;
  for (int i = 0; i < n; i++) buf_[i] = bg;
}

void Stripe::drawPixel(int16_t x, int16_t y, uint16_t color) {
  if (x < rx_ || x >= rx_ + rw_ || y < sy_ || y >= sy_ + sh_) return;
  buf_[(y - sy_) * rw_ + (x - rx_)] = color;
}

void Stripe::writeFillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {
  int x0 = x < rx_ ? rx_ : x;
  int y0 = y < sy_ ? sy_ : y;
  int x1 = (x + w) > (rx_ + rw_) ? (rx_ + rw_) : (x + w);
  int y1 = (y + h) > (sy_ + sh_) ? (sy_ + sh_) : (y + h);
  if (x0 >= x1 || y0 >= y1) return;
  for (int yy = y0; yy < y1; yy++) {
    uint16_t* row = buf_ + (yy - sy_) * rw_ + (x0 - rx_);
    for (int xx = x0; xx < x1; xx++) *row++ = color;
  }
}

void Stripe::writeFastHLine(int16_t x, int16_t y, int16_t w, uint16_t color) {
  writeFillRect(x, y, w, 1, color);
}

void Stripe::writeFastVLine(int16_t x, int16_t y, int16_t h, uint16_t color) {
  writeFillRect(x, y, 1, h, color);
}

// ---------- Lcd7 ----------
void Lcd7::begin() {
  buildTables();

  neopixelWrite(48, 0, 0, 0);          // eingebaute RGB-LED aus (wie im Test-Code)

  pinMode(PIN_BL, OUTPUT);
  digitalWrite(PIN_BL, HIGH);          // Hintergrundbeleuchtung an

  pinMode(PIN_CS, OUTPUT);
  pinMode(PIN_RS, OUTPUT);
  pinMode(PIN_WR, OUTPUT);
  pinMode(PIN_RST, OUTPUT);
  digitalWrite(PIN_WR, HIGH);
  for (int i = 0; i < 16; i++) pinMode(DB_PINS[i], OUTPUT);

  digitalWrite(PIN_CS, LOW);           // das Display ist das einzige Gerät am Bus: CS bleibt dauerhaft aktiv
  initController();
}

void Lcd7::initController() {
  constexpr uint8_t W_HI = (LCD_W - 1) >> 8, W_LO = (LCD_W - 1) & 0xFF;
  constexpr uint8_t H_HI = (LCD_H - 1) >> 8, H_LO = (LCD_H - 1) & 0xFF;
  constexpr int HT = LCD_W + 128;
  constexpr int VT = LCD_H + 45;
  constexpr uint8_t HT_HI = (HT - 1) >> 8, HT_LO = (HT - 1) & 0xFF;
  constexpr uint8_t VT_HI = (VT - 1) >> 8, VT_LO = (VT - 1) & 0xFF;

  digitalWrite(PIN_RST, HIGH);
  delay(20);
  digitalWrite(PIN_RST, LOW);
  delay(50);
  digitalWrite(PIN_RST, HIGH);
  delay(150);

  // Ab hier unverändert aus dem funktionierenden Test-Code
  cd(0xE2, {0x23, 0x02, 0x04});           // PLL einstellen
  cd(0xE0, {0x01});  delay(10);           // PLL starten
  cd(0xE0, {0x03});  delay(10);           // PLL als Takt verwenden
  cd(0x01, {});      delay(20);           // Software-Reset
  cd(0xE6, {0x03, 0x33, 0x33});           // Pixeltakt (800x480)
  cd(0xB0, {0x20, 0x00, W_HI, W_LO, H_HI, H_LO, 0x00});
  cd(0xB4, {HT_HI, HT_LO, 0x00, 46, 48, 0x00, 0x00, 0x00});
  cd(0xB6, {VT_HI, VT_LO, 0x00, 23, 16, 0x00, 0x00});
  cd(0xBA, {0x0F, 0x01});
  cd(0xB8, {0x0F, 0x01});
  cd(0x36, {LCD_ADDRESS_MODE});           // Bildausrichtung (siehe lcd7.h)
  cd(0xF0, {0x03});                        // 16-Bit-Pixeldaten (RGB565)
  cd(0xBE, {0x06, 0xFF, 0x01, 0xFF, 0x00, 0x00});
  cd(0x11, {});      delay(20);           // Sleep aus
  cd(0x29, {});                            // Display an
}

void Lcd7::setWindow(int x, int y, int w, int h) {
  const int x2 = x + w - 1;
  const int y2 = y + h - 1;
  writeCmd(0x2A);
  writeData(x >> 8); writeData(x & 0xFF); writeData(x2 >> 8); writeData(x2 & 0xFF);
  writeCmd(0x2B);
  writeData(y >> 8); writeData(y & 0xFF); writeData(y2 >> 8); writeData(y2 & 0xFF);
  writeCmd(0x2C);
}

void Lcd7::pushPixels(const uint16_t* px, uint32_t n) {
  REG_WRITE(GPIO_OUT_W1TS_REG, RS_MASK);   // Daten, keine Befehle
  for (uint32_t i = 0; i < n; i++) {
    busPut(px[i]);
    strobe();
  }
}

void Lcd7::fillRect(int x, int y, int w, int h, uint16_t color) {
  if (x < 0) { w += x; x = 0; }
  if (y < 0) { h += y; y = 0; }
  if (x + w > LCD_W) w = LCD_W - x;
  if (y + h > LCD_H) h = LCD_H - y;
  if (w <= 0 || h <= 0) return;

  setWindow(x, y, w, h);
  REG_WRITE(GPIO_OUT_W1TS_REG, RS_MASK);
  busPut(color);                            // Farbe einmal anlegen, dann nur noch WR toggeln
  const uint32_t n = (uint32_t)w * h;
  for (uint32_t i = 0; i < n; i++) strobe();
}

void Lcd7::render(int x, int y, int w, int h, uint16_t bg, SceneFn scene) {
  if (x < 0 || y < 0 || w <= 0 || h <= 0 || x + w > LCD_W || y + h > LCD_H) return;
  for (int sy = y; sy < y + h; sy += STRIPE_H) {
    const int sh = (sy + STRIPE_H <= y + h) ? STRIPE_H : (y + h - sy);
    stripe.setStripe(x, w, sy, sh, bg);
    scene(stripe);
    setWindow(x, sy, w, sh);
    pushPixels(stripeBuf, (uint32_t)w * sh);
  }
}
