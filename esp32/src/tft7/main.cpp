// FlightScanr, Variante mit 7"-Display (ESP32-S3).
//
// Das Netzwerk läuft wie beim OLED in einem eigenen Task (network_task.cpp).
// Diese Schleife kümmert sich nur um Touch und Anzeige und wartet nie auf das Netz.

#include <Arduino.h>
#include "config.h"
#include "network_task.h"
#include "lcd7.h"
#include "touch.h"
#include "gui.h"

static Lcd7             lcd;
static AircraftSnapshot snap;          // lokale Kopie der aktuellen Daten
static uint32_t         seenVersion = 0;

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\nFlightScanr 7\" gestartet");

  lcd.begin();
  lcd.fillRect(0, 0, LCD_W, LCD_H, 0x0000);
  touchBegin(lcd);        // lädt die Kalibrierung (oder startet sie beim ersten Mal)
  guiBegin(lcd);
  networkTaskStart();     // WLAN verbinden und Daten holen, läuft im Hintergrund
}

void loop() {
  static bool     wasTouched = false;
  static uint32_t lastTap = 0, frames = 0, lastPrint = 0;

  // Touch: nur der Moment des Auflegens zählt
  int16_t tx, ty;
  if (touchGet(tx, ty)) {
    if (!wasTouched && millis() - lastTap > 200) {
      guiTap(tx, ty);
      lastTap = millis();
    }
    wasTouched = true;
  } else {
    wasTouched = false;
  }

  if (networkGetSnapshot(snap, seenVersion)) {
    guiNewData(snap);
  }

  guiRender();

  frames++;
  if (millis() - lastPrint >= 5000) {
    Serial.printf("Bilder pro Sekunde: %lu\n", (unsigned long)(frames / 5));
    frames = 0;
    lastPrint = millis();
  }
}
