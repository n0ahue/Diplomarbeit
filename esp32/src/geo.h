#pragma once

// Rechnet Lat/Lon in Pixelkoordinaten um (Radar-Mittelpunkt cx/cy, Radius r in Pixel).
// Reine Rechenlogik, unabhängig vom Display.
void toRadar(float lat, float lon, int cx, int cy, int r, int& px, int& py);

// Entfernung zum Antennenstandort in km (einfache Näherung, reicht für ~400 km).
float distanceKm(float lat, float lon);
