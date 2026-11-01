#pragma once

// Rechnet Lat/Lon in Pixelkoordinaten um (Radar-Mittelpunkt cx/cy, Radius r in Pixel).
// Reine Rechenlogik, unabhängig vom Display.
void toRadar(float lat, float lon, int cx, int cy, int r, int& px, int& py);
