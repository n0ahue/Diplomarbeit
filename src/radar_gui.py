import socket
import time
import math
import threading
import pygame
import sys

# --- KONFIGURATION ---
PI_IP = "127.0.0.1"       # Am Pi "127.0.0.1", am Laptop die IP des Pi
PORT = 30003
TIMEOUT_SECONDS = 60

HOME_LAT = 47.82194
HOME_LON = 13.04569
MAX_RANGE_KM = 100.0      # Maximaler Radar-Radius auf dem Bildschirm (100 km)

# Farben (RGB)
COLOR_BG = (10, 15, 20)
COLOR_GRID = (0, 100, 50)
COLOR_TEXT = (0, 255, 128)
COLOR_AIRCRAFT = (0, 255, 200)
COLOR_SELECTED = (255, 200, 0)

def calc_distance_and_bearing(lat1, lon1, lat2, lon2):
    R = 6371.0
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
        self.lock = threading.Lock()

    def process_line(self, line):
        parts = line.strip().split(",")
        if len(parts) < 16:
            return

        msg_type = parts[1]
        icao_hex = parts[4]
        if not icao_hex:
            return

        now = time.time()

        with self.lock:
            if icao_hex not in self.aircrafts:
                self.aircrafts[icao_hex] = {
                    "callsign": "N/A",
                    "alt": 0,
                    "speed": 0,
                    "track": 0.0,
                    "lat": 0.0,
                    "lon": 0.0,
                    "dist_km": 0.0,
                    "bearing": 0.0,
                    "last_seen": now
                }

            self.aircrafts[icao_hex]["last_seen"] = now

            if msg_type == "1":
                call = parts[10].strip()
                if call:
                    self.aircrafts[icao_hex]["callsign"] = call

            elif msg_type == "3":
                if parts[11]:
                    self.aircrafts[icao_hex]["alt"] = int(parts[11])
                if parts[14] and parts[15]:
                    lat = float(parts[14])
                    lon = float(parts[15])
                    self.aircrafts[icao_hex]["lat"] = lat
                    self.aircrafts[icao_hex]["lon"] = lon
                    dist, bearing = calc_distance_and_bearing(HOME_LAT, HOME_LON, lat, lon)
                    self.aircrafts[icao_hex]["dist_km"] = dist
                    self.aircrafts[icao_hex]["bearing"] = bearing

            elif msg_type == "4":
                if len(parts) > 12 and parts[12]:
                    self.aircrafts[icao_hex]["speed"] = int(float(parts[12]))
                if len(parts) > 13 and parts[13]:
                    self.aircrafts[icao_hex]["track"] = float(parts[13])

    def cleanup(self):
        now = time.time()
        with self.lock:
            expired = [icao for icao, data in self.aircrafts.items() if now - data["last_seen"] > TIMEOUT_SECONDS]
            for icao in expired:
                del self.aircrafts[icao]

    def get_snapshot(self):
        with self.lock:
            return dict(self.aircrafts)

def socket_worker(tracker):
    """Hintergrund-Thread für den Datenempfang"""
    while True:
        try:
            s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            s.settimeout(2.0)
            s.connect((PI_IP, PORT))
            buffer = ""
            while True:
                try:
                    data = s.recv(2048).decode("utf-8", errors="ignore")
                    if not data:
                        break
                    buffer += data
                    lines = buffer.split("\n")
                    buffer = lines.pop()
                    for line in lines:
                        tracker.process_line(line)
                except socket.timeout:
                    pass
                tracker.cleanup()
        except Exception:
            time.sleep(2.0)  # Reconnect-Versuch bei Verbindungsverlust

def main():
    tracker = AircraftTracker()
    
    # Empfangs-Thread starten
    t = threading.Thread(target=socket_worker, args=(tracker,), daemon=True)
    t.start()

    pygame.init()
    screen = pygame.display.set_mode((800, 800))
    pygame.display.set_caption("FlightScanr - ADS-B 2D Radar")
    clock = pygame.time.Clock()
    font = pygame.font.SysFont("monospace", 12)
    font_large = pygame.font.SysFont("monospace", 16, bold=True)

    center_x, center_y = 400, 400
    radar_radius = 350  # Radius des Radarkreises auf dem Schirm in Pixeln

    running = True
    while running:
        for event in pygame.event.get():
            if event.type == pygame.QUIT or (event.type == pygame.KEYDOWN and event.key == pygame.K_ESCAPE):
                running = False

        screen.fill(COLOR_BG)

        # 1. RADAR-RASTER ZEICHNEN
        # Konzentrische Entfernungsringe (25 km, 50 km, 75 km, 100 km)
        for r_km in [25, 50, 75, 100]:
            r_px = int((r_km / MAX_RANGE_KM) * radar_radius)
            pygame.draw.circle(screen, COLOR_GRID, (center_x, center_y), r_px, 1)
            txt = font.render(f"{r_km} km", True, COLOR_GRID)
            screen.blit(txt, (center_x + 5, center_y - r_px - 10))

        # Fadenkreuz (N-S, W-O)
        pygame.draw.line(screen, COLOR_GRID, (center_x - radar_radius, center_y), (center_x + radar_radius, center_y), 1)
        pygame.draw.line(screen, COLOR_GRID, (center_x, center_y - radar_radius), (center_x, center_y + radar_radius), 1)

        # Himmelsrichtungen beschriften
        screen.blit(font_large.render("N", True, COLOR_GRID), (center_x - 5, center_y - radar_radius - 20))
        screen.blit(font_large.render("S", True, COLOR_GRID), (center_x - 5, center_y + radar_radius + 5))
        screen.blit(font_large.render("O", True, COLOR_GRID), (center_x + radar_radius + 10, center_y - 10))
        screen.blit(font_large.render("W", True, COLOR_GRID), (center_x - radar_radius - 25, center_y - 10))

        # 2. FLUGZEUGE ZEICHNEN
        aircrafts = tracker.get_snapshot()
        for icao, data in aircrafts.items():
            dist_km = data["dist_km"]
            bearing = data["bearing"]

            if dist_km > 0 and dist_km <= MAX_RANGE_KM:
                # Umrechnung Polar in Bildschirmkoordinaten
                angle_rad = math.radians(bearing)
                dist_px = (dist_km / MAX_RANGE_KM) * radar_radius
                
                px = int(center_x + dist_px * math.sin(angle_rad))
                py = int(center_y - dist_px * math.cos(angle_rad))

                # Flugzeug als Symbol zeichnen
                pygame.draw.circle(screen, COLOR_AIRCRAFT, (px, py), 5)

                # Kursvektor (Richtungslinie zeichnen)
                if data["track"] > 0:
                    track_rad = math.radians(data["track"])
                    vec_x = int(px + 15 * math.sin(track_rad))
                    vec_y = int(py - 15 * math.cos(track_rad))
                    pygame.draw.line(screen, COLOR_AIRCRAFT, (px, py), (vec_x, vec_y), 2)

                # Label (Callsign & Höhe) neben das Symbol setzen
                label_text = f"{data['callsign']} | {data['alt']}ft"
                txt_surface = font.render(label_text, True, COLOR_TEXT)
                screen.blit(txt_surface, (px + 8, py - 8))

        # Statusleiste oben links
        status_txt = f"Aktive Flugzeuge: {len(aircrafts)} | Max. Reichweite: {MAX_RANGE_KM:.0f} km"
        screen.blit(font_large.render(status_txt, True, COLOR_TEXT), (15, 15))

        pygame.display.flip()
        clock.tick(30)  # 30 FPS

    pygame.quit()
    sys.exit()

if __name__ == "__main__":
    main()