#include "airlines.h"
#include <string.h>

// ICAO-Airline-Codes (die ersten drei Buchstaben des Callsigns).
// Die Tabelle lässt sich beliebig erweitern: einfach eine Zeile {"CODE", "Name"} ergänzen.
// Namen kurz halten, auf dem OLED passen etwa 20 Zeichen in eine Zeile.
struct Airline {
  const char code[4];
  const char* name;
};

static const Airline AIRLINES[] = {
  // Österreich, Deutschland, Schweiz
  {"AUA", "Austrian Airlines"},
  {"LDM", "Lauda"},
  {"DLH", "Lufthansa"},
  {"EWG", "Eurowings"},
  {"CFG", "Condor"},
  {"TUI", "TUIfly"},
  {"SWR", "Swiss"},
  {"GEC", "Lufthansa Cargo"},
  {"BCS", "European Air Transport"},
  // Billigfluglinien
  {"RYR", "Ryanair"},
  {"RUK", "Ryanair UK"},
  {"EZY", "easyJet"},
  {"EJU", "easyJet Europe"},
  {"WZZ", "Wizz Air"},
  {"VLG", "Vueling"},
  {"VOE", "Volotea"},
  {"NAX", "Norwegian"},
  {"EXS", "Jet2"},
  {"TOM", "TUI Airways"},
  {"TRA", "Transavia"},
  {"SXS", "SunExpress"},
  {"PGT", "Pegasus"},
  // Europa
  {"BAW", "British Airways"},
  {"AFR", "Air France"},
  {"KLM", "KLM"},
  {"IBE", "Iberia"},
  {"AEA", "Air Europa"},
  {"TAP", "TAP Air Portugal"},
  {"ITY", "ITA Airways"},
  {"SAS", "SAS"},
  {"FIN", "Finnair"},
  {"ICE", "Icelandair"},
  {"EIN", "Aer Lingus"},
  {"BEL", "Brussels Airlines"},
  {"LGL", "Luxair"},
  {"LOT", "LOT Polish"},
  {"CSA", "Czech Airlines"},
  {"TVS", "Smartwings"},
  {"ROT", "Tarom"},
  {"LZB", "Bulgaria Air"},
  {"CTN", "Croatia Airlines"},
  {"ASL", "Air Serbia"},
  {"AEE", "Aegean"},
  {"BTI", "airBaltic"},
  {"AUI", "Ukraine Intl"},
  {"THY", "Turkish Airlines"},
  // Naher Osten, Afrika
  {"UAE", "Emirates"},
  {"QTR", "Qatar Airways"},
  {"ETD", "Etihad"},
  {"ELY", "El Al"},
  {"MSR", "EgyptAir"},
  {"ETH", "Ethiopian"},
  // Asien, Amerika
  {"AIC", "Air India"},
  {"SIA", "Singapore Airlines"},
  {"CPA", "Cathay Pacific"},
  {"ANA", "All Nippon Airways"},
  {"JAL", "Japan Airlines"},
  {"KAL", "Korean Air"},
  {"AAR", "Asiana"},
  {"AAL", "American Airlines"},
  {"DAL", "Delta"},
  {"UAL", "United"},
  {"ACA", "Air Canada"},
  // Fracht
  {"UPS", "UPS"},
  {"FDX", "FedEx"},
  {"CLX", "Cargolux"},
};

static const int AIRLINE_COUNT = sizeof(AIRLINES) / sizeof(AIRLINES[0]);

const char* airlineName(const char* callsign) {
  if (!callsign || strlen(callsign) < 4) return nullptr;

  // Linienflüge: drei Buchstaben, danach eine Ziffer (z. B. "AUA123", "DLH4AB").
  // Alles andere (z. B. Kennzeichen wie "DABCD" oder "N/A") ist kein Linienflug.
  for (int i = 0; i < 3; i++) {
    if (callsign[i] < 'A' || callsign[i] > 'Z') return nullptr;
  }
  if (callsign[3] < '0' || callsign[3] > '9') return nullptr;

  for (int i = 0; i < AIRLINE_COUNT; i++) {
    if (strncmp(AIRLINES[i].code, callsign, 3) == 0) return AIRLINES[i].name;
  }
  return nullptr;
}
