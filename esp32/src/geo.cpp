#include <Arduino.h>
#include <math.h>
#include "geo.h"
#include "config.h"

void toRadar(float lat, float lon, int cx, int cy, int r, int& px, int& py) {
  float dy = (lat - HOME_LAT) * 111.0f;                                // km nach Norden
  float dx = (lon - HOME_LON) * 111.0f * cosf(HOME_LAT * DEG_TO_RAD);  // km nach Osten
  float scale = r / RANGE_KM;
  px = cx + (int)(dx * scale);
  py = cy - (int)(dy * scale);
}
