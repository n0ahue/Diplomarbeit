import socket
import time
import os

PI_IP = "127.0.0.1"  # Auf dem Pi "127.0.0.1", vom Laptop aus die Pi-IP
PORT = 30003
TIMEOUT_SECONDS = 60  # Flugzeuge nach 60s Inaktivität entfernen

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

        # Neues Flugzeug im Speicher anlegen
        if icao_hex not in self.aircrafts:
            self.aircrafts[icao_hex] = {
                "callsign": "N/A",
                "alt": 0,
                "lat": 0.0,
                "lon": 0.0,
                "last_seen": now
            }

        self.aircrafts[icao_hex]["last_seen"] = now

        # MSG 1: Rufzeichen
        if msg_type == "1":
            call = parts[10].strip()
            if call:
                self.aircrafts[icao_hex]["callsign"] = call

        # MSG 3: Position & Höhe
        elif msg_type == "3":
            if parts[11]:
                self.aircrafts[icao_hex]["alt"] = int(parts[11])
            if parts[14] and parts[15]:
                self.aircrafts[icao_hex]["lat"] = float(parts[14])
                self.aircrafts[icao_hex]["lon"] = float(parts[15])

        # Inaktive Flugzeuge aufräumen
        self.cleanup()

    def cleanup(self):
        now = time.time()
        expired = [icao for icao, data in self.aircrafts.items() if now - data["last_seen"] > TIMEOUT_SECONDS]
        for icao in expired:
            del self.aircrafts[icao]

    def print_status(self):
        """Gibt eine aufgeräumte Live-Tabelle im Terminal aus"""
        os.system('cls' if os.name == 'nt' else 'clear')
        print("=" * 80)
        print(f"{'ICAO':<8} | {'CALLSIGN':<10} | {'HÖHE (ft)':<10} | {'LAT':<10} | {'LON':<10} | {'INAKTIV (s)':<10}")
        print("=" * 80)
        
        now = time.time()
        for icao, data in self.aircrafts.items():
            age = int(now - data["last_seen"])
            print(f"{icao:<8} | {data['callsign']:<10} | {data['alt']:<10} | {data['lat']:<10.4f} | {data['lon']:<10.4f} | {age:<10}")
        
        print("=" * 80)
        print(f"Aktive Flugzeuge im Empfangsbereich: {len(self.aircrafts)}\n")

if __name__ == "__main__":
    tracker = AircraftTracker()
    print(f"Verbinde mit readsb auf {PI_IP}:{PORT}...")
    
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s.connect((PI_IP, PORT))
        buffer = ""
        last_display_update = 0

        while True:
            data = s.recv(2048).decode("utf-8", errors="ignore")
            if not data:
                break
            buffer += data
            lines = buffer.split("\n")
            buffer = lines.pop()

            for line in lines:
                tracker.process_line(line)

            # Bildschirm nur alle 1 Sekunde aktualisieren
            if time.time() - last_display_update > 1.0:
                tracker.print_status()
                last_display_update = time.time()

    except KeyboardInterrupt:
        print("\nProgramm beendet.")
    except Exception as e:
        print(f"Fehler: {e}")