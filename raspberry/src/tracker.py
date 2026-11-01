import socket
import time
import os
import math

PI_IP = "127.0.0.1"  # Am Pi "127.0.0.1", am Laptop die IP des Pi
PORT = 30003
TIMEOUT_SECONDS = 60

# Empfängerkoordinaten (Standort Salzburg)
HOME_LAT = 47.82194
HOME_LON = 13.04569

def calc_distance_and_bearing(lat1, lon1, lat2, lon2):
    """Berechnet Entfernung in km und Peilung in Grad (Azimut von eurer Station aus)"""
    R = 6371.0  # Erdradius in km
    dlat = math.radians(lat2 - lat1)
    dlon = math.radians(lon2 - lon1)
    
    a = math.sin(dlat / 2)**2 + math.cos(math.radians(lat1)) * math.cos(math.radians(lat2)) * math.sin(dlon / 2)**2
    c = 2 * math.atan2(math.sqrt(a), math.sqrt(1 - a))
    dist_km = R * c

    y = math.sin(dlon) * math.cos(math.radians(lat2))
    x = math.cos(math.radians(lat1)) * math.sin(math.radians(lat2)) - math.sin(math.radians(lat1)) * math.cos(math.radians(lat2)) * math.cos(dlon)
    bearing = (math.degrees(math.atan2(y, x)) + 360) % 360

    return dist_km, bearing

class AircraftTracker:
    def __init__(self):
        self.aircrafts = {}

    def process_line(self, line):
        parts = line.strip().split(",")
        if len(parts) < 16:
            return

        msg_type = parts[1]
        icao_hex = parts[4]

        if not icao_hex:
            return

        now = time.time()

        # Neues Flugzeug anlegen
        if icao_hex not in self.aircrafts:
            self.aircrafts[icao_hex] = {
                "callsign": "N/A",
                "alt": 0,
                "speed": 0,      # Geschwindigkeit in Knoten (kt)
                "track": 0.0,    # Flugkurs in Grad (Richtung, in die das Flugzeug fliegt)
                "lat": 0.0,
                "lon": 0.0,
                "dist_km": 0.0,
                "bearing": 0.0,  # Peilung von eurer Station aus
                "last_seen": now
            }

        self.aircrafts[icao_hex]["last_seen"] = now

        # MSG 1: Rufzeichen (Callsign)
        if msg_type == "1":
            call = parts[10].strip()
            if call:
                self.aircrafts[icao_hex]["callsign"] = call

        # MSG 3: Position & Höhe
        elif msg_type == "3":
            if parts[11]:
                self.aircrafts[icao_hex]["alt"] = int(parts[11])
            if parts[14] and parts[15]:
                lat = float(parts[14])
                lon = float(parts[15])
                self.aircrafts[icao_hex]["lat"] = lat
                self.aircrafts[icao_hex]["lon"] = lon
                
                # Distanz und Peilung von eurer Station berechnen
                dist, bearing = calc_distance_and_bearing(HOME_LAT, HOME_LON, lat, lon)
                self.aircrafts[icao_hex]["dist_km"] = dist
                self.aircrafts[icao_hex]["bearing"] = bearing

        # MSG 4: Geschwindigkeit & Flugkurs (Track)
        elif msg_type == "4":
            if len(parts) > 12 and parts[12]:
                self.aircrafts[icao_hex]["speed"] = int(float(parts[12]))
            if len(parts) > 13 and parts[13]:
                self.aircrafts[icao_hex]["track"] = float(parts[13])

    def cleanup(self):
        now = time.time()
        expired = [icao for icao, data in self.aircrafts.items() if now - data["last_seen"] > TIMEOUT_SECONDS]
        for icao in expired:
            del self.aircrafts[icao]

    def print_status(self):
        os.system('cls' if os.name == 'nt' else 'clear')
        print("=" * 115)
        print(f"{'ICAO':<8} | {'CALLSIGN':<10} | {'HÖHE (ft)':<10} | {'SPEED (kt)':<10} | {'KURS (°)':<9} | {'DIST (km)':<10} | {'PEILUNG (°)':<11} | {'INAKTIV (s)':<10}")
        print("=" * 115)
        
        now = time.time()
        for icao, data in self.aircrafts.items():
            age = int(now - data["last_seen"])
            dist_str = f"{data['dist_km']:.1f}" if data['dist_km'] > 0 else "N/A"
            bear_str = f"{data['bearing']:.0f}°" if data['dist_km'] > 0 else "N/A"
            track_str = f"{data['track']:.0f}°" if data['track'] > 0 else "N/A"
            speed_str = f"{data['speed']}" if data['speed'] > 0 else "N/A"

            print(f"{icao:<8} | {data['callsign']:<10} | {data['alt']:<10} | {speed_str:<10} | {track_str:<9} | {dist_str:<10} | {bear_str:<11} | {age:<10}")
        
        print("=" * 115)
        print(f"Standort Empfänger: {HOME_LAT}, {HOME_LON} | Aktive Flugzeuge: {len(self.aircrafts)}\n")

if __name__ == "__main__":
    tracker = AircraftTracker()
    print(f"Verbinde mit readsb auf {PI_IP}:{PORT}...")
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s.settimeout(1.0)
        s.connect((PI_IP, PORT))
        
        buffer = ""
        last_display_update = 0

        tracker.print_status()

        while True:
            try:
                data = s.recv(2048).decode("utf-8", errors="ignore")
                if data:
                    buffer += data
                    lines = buffer.split("\n")
                    buffer = lines.pop()

                    for line in lines:
                        tracker.process_line(line)
            except socket.timeout:
                pass

            if time.time() - last_display_update >= 1.0:
                tracker.cleanup()
                tracker.print_status()
                last_display_update = time.time()

    except KeyboardInterrupt:
        print("\nProgramm beendet.")
    except Exception as e:
        print(f"Fehler: {e}")