#pragma once
#include <Arduino.h>

void radiusSwitchInit();

// Liefert den Radius, der zur aktuellen Schalterstellung gehört (entprellt).
// Kann beliebig oft aufgerufen werden, am besten in jedem Durchlauf von loop().
float radiusSwitchKm();
