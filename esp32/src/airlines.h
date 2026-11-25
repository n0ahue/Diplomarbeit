#pragma once

// Liefert den Namen der Fluggesellschaft zu einem Callsign (z. B. "AUA123" -> "Austrian Airlines").
// Rückgabe: nullptr, wenn das Callsign nicht nach dem Muster "3 Buchstaben + Ziffer" aufgebaut
// ist (Privat-, Militär- oder Sonderflüge) oder die Gesellschaft nicht in der Tabelle steht.
// Die Tabelle liegt fest im Flash, es wird keine Verbindung zum Internet gebraucht.
const char* airlineName(const char* callsign);
