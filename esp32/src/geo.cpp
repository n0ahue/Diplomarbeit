#include <Arduino.h>
#include <math.h>
#include "geo.h"
#include "config.h"

static void offsetsKm(float lat, float lon, float& dxKm, float& dyKm) {
  dyKm = (lat - HOME_LAT) * 111.0f;                                // nach Norden
  dxKm = (lon - HOME_LON) * 111.0f * cosf(HOME_LAT * DEG_TO_RAD);  // nach Osten
}

void toRadar(float lat, float lon, int cx, int cy, int r, int& px, int& py) {
  float dx, dy;
  offsetsKm(lat, lon, dx, dy);
  float scale = r / RANGE_KM;
  px = cx + (int)(dx * scale);
  py = cy - (int)(dy * scale);
}

float distanceKm(float lat, float lon) {
  float dx, dy;
  offsetsKm(lat, lon, dx, dy);
  return sqrtf(dx * dx + dy * dy);
}
