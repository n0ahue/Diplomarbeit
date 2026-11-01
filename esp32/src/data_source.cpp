#include "data_source.h"

// Beispiel im Format des api_server.py
static const char* SAMPLE_JSON = R"({
 "active_count": 3,
 "data": [
  {"hex":"4B1805","call":"AUA123","alt":36000,"spd":450,"hdg":90.0,"lat":47.3,"lon":15.5,"type":"A320"},
  {"hex":"3C6444","call":"DLH4AB","alt":28000,"spd":420,"hdg":200.0,"lat":46.6,"lon":14.7,"type":"B737"},
  {"hex":"4CA123","call":"N/A","alt":12000,"spd":250,"hdg":330.0,"lat":47.0,"lon":15.2,"type":"GENERIC"}
 ]
})";

bool fetchAircraftJson(char* buf, size_t len) {
  // TODO (Schritt 1): HTTP-GET http://<Pi-IP>:8080 über W5500
  strlcpy(buf, SAMPLE_JSON, len);
  return true;
}
