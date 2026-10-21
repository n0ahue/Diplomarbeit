import json
import os
from http.server import BaseHTTPRequestHandler, HTTPServer

# Pfad zur originalen readsb-Datenbank auf dem Pi
READSB_JSON = "/run/readsb/aircraft.json"
PORT = 8080

def get_compact_radar_data():
    """Liest die große Datei und erstellt ein kompaktes Mini-JSON für den ESP32"""
    if not os.path.exists(READSB_JSON):
        return {"error": "readsb JSON nicht gefunden. Läuft readsb?"}
        
    try:
        with open(READSB_JSON, "r") as f:
            data = json.load(f)

        compact_aircraft = []
        for ac in data.get("aircraft", []):
            # Wir senden dem ESP32 nur Flugzeuge, die eine bekannte Position haben
            if ac.get("lat") and ac.get("lon"):
                compact_aircraft.append({
                    "hex": ac.get("hex", "000000").upper(),
                    "call": ac.get("flight", "").strip() or "N/A",
                    "alt": ac.get("alt_baro", 0),
                    "spd": ac.get("gs", 0),
                    "hdg": ac.get("track", 0.0),
                    "lat": ac.get("lat"),
                    "lon": ac.get("lon"),
                    "type": ac.get("t", "GENERIC")
                })
        
        return {
            "active_count": len(compact_aircraft),
            "data": compact_aircraft
        }
    except Exception as e:
        return {"error": str(e)}

class ESP32RadarAPI(BaseHTTPRequestHandler):
    def do_GET(self):
        # Der ESP32 wird später http://<PI_IP>:8080/api/radar aufrufen
        if self.path == '/api/radar':
            # HTTP Header senden
            self.send_response(200)
            self.send_header('Content-type', 'application/json')
            self.send_header('Access-Control-Allow-Origin', '*')
            self.end_headers()
            
            # Daten komprimieren und an den ESP32 (oder Laptop) senden
            json_data = json.dumps(get_compact_radar_data())
            self.wfile.write(json_data.encode('utf-8'))
        else:
            self.send_response(404)
            self.end_headers()

    # Unterdrücke die Standard-Logs in der Konsole, damit es clean bleibt
    def log_message(self, format, *args):
        pass

def main():
    server_address = ('0.0.0.0', PORT)
    httpd = HTTPServer(server_address, ESP32RadarAPI)
    print("==================================================")
    print(f"🚀 API-Server für ESP32 gestartet auf Port {PORT}")
    print("==================================================")
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        print("\nAPI-Server beendet.")
        httpd.server_close()

if __name__ == '__main__':
    main()