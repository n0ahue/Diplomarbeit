import socket
import time

# IP des Raspberry Pi eintragen (oder "127.0.0.1" für lokalen Test am Pi)
PI_IP = "127.0.0.1"  # Hier die IP deines Raspberry Pi eintragen
PORT = 30003

class AircraftTracker:
    def __init__(self):
        self.aircrafts = {} # Hex-ID -> Daten

    def process_line(self, line):
        parts = line.strip().split(",")
        if len(parts) < 16:
            return

        msg_type = parts[1]
        icao_hex = parts[4]

        if icao_hex not in self.aircrafts:
            self.aircrafts[icao_hex] = {"callsign": "", "alt": 0, "lat": 0.0, "lon": 0.0, "last_seen": time.time()}

        self.aircrafts[icao_hex]["last_seen"] = time.time()

        # MSG 1: Rufzeichen
        if msg_type == "1":
            self.aircrafts[icao_hex]["callsign"] = parts[10].strip()
        # MSG 3: Position
        elif msg_type == "3":
            if parts[11]: self.aircrafts[icao_hex]["alt"] = int(parts[11])
            if parts[14] and parts[15]:
                self.aircrafts[icao_hex]["lat"] = float(parts[14])
                self.aircrafts[icao_hex]["lon"] = float(parts[15])

        print(f"Aktive Flugzeuge: {len(self.aircrafts)} | Letztes Update: {icao_hex} -> {self.aircrafts[icao_hex]}")

if __name__ == "__main__":
    tracker = AircraftTracker()
    print(f"Verbinde mit readsb auf {PI_IP}:{PORT}...")
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s.connect((PI_IP, PORT))
        buffer = ""
        while True:
            data = s.recv(2048).decode("utf-8", errors="ignore")
            if not data: break
            buffer += data
            lines = buffer.split("\n")
            buffer = lines.pop()
            for line in lines:
                tracker.process_line(line)
    except Exception as e:
        print(f"Hinweis: Verbindung zum Pi fehlgeschlagen ({e}). Überprüfe die IP oder teste am Pi!")