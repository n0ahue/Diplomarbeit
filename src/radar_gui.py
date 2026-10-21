import socket
import time
import math
import threading
import datetime
import os
import json
import pygame
import sys

# --- KONFIGURATION ---
PI_IP = "127.0.0.1"       # Am Pi "127.0.0.1", am Laptop die IP des Pi
PORT = 30003
TIMEOUT_SECONDS = 60

HOME_LAT = 47.82194
HOME_LON = 13.04569
MAX_RANGE_KM = 100.0

# Ultra-Clean Dark Theme (wie auf dem Beispielfoto)
COLOR_BG = (15, 16, 18)          # Sehr dunkles Graphit-Schwarz
COLOR_TEXT_MAIN = (255, 255, 255) # Reinweiß für Haupttexte
COLOR_TEXT_DIM = (130, 135, 145) # Dezentes Grau für Labels
COLOR_GRID = (45, 50, 60)        # Feines Radar-Raster
COLOR_RADAR_BLIP = (255, 255, 255)

# Bekannte ICAO-Hex Präfixe / Test-Zuordnung für Flugzeugtypen
AIRCRAFT_DB = {
    # Beispiel-Zuordnungen (wird automatisch ergänzt/ausgelesen)
    "3C56F5": {"type": "A320", "name": "AIRBUS A320-200", "operator": "EUROWINGS", "reg": "D-AIZP"},
    "4D2261": {"type": "B737", "name": "BOEING 737-800", "operator": "RYANAIR", "reg": "EI-DHP"},
    "300A7C": {"type": "A320", "name": "AIRBUS A320neo", "operator": "LUFTHANSA", "reg": "D-AINA"},
    "501C5B": {"type": "B737", "name": "BOEING 737 MAX 8", "operator": "TURKISH AIRLINES", "reg": "TC-LCA"},
}

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
                # Typen-Info aus DB oder Fallback holen
                db_info = AIRCRAFT_DB.get(icao_hex, {
                    "type": "GENERIC",
                    "name": "PASSENGER AIRCRAFT",
                    "operator": "COMMERCIAL AIRLINE",
                    "reg": icao_hex
                })

                self.aircrafts[icao_hex] = {
                    "callsign": "N/A",
                    "alt": 0,
                    "speed": 0,
                    "track": 0.0,
                    "lat": 0.0,
                    "lon": 0.0,
                    "dist_km": 0.0,
                    "bearing": 0.0,
                    "type": db_info["type"],
                    "name": db_info["name"],
                    "operator": db_info["operator"],
                    "reg": db_info["reg"],
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
            time.sleep(2.0)

class ImageLoader:
    """Lädt und skaliert Flugzeugbilder dynamisch aus dem assets/ Ordner"""
    def __init__(self):
        self.cache = {}

    def get_image(self, ac_type, target_width):
        if ac_type in self.cache:
            return self.cache[ac_type]

        filepath = f"assets/{ac_type}.png"
        if not os.path.exists(filepath):
            filepath = "assets/GENERIC.png"

        if os.path.exists(filepath):
            try:
                img = pygame.image.load(filepath).convert_alpha()
                # Proportional skalieren
                aspect = img.get_height() / img.get_width()
                new_h = int(target_width * aspect)
                scaled = pygame.transform.smoothscale(img, (target_width, new_h))
                self.cache[ac_type] = scaled
                return scaled
            except Exception:
                pass
        return None

def main():
    tracker = AircraftTracker()
    img_loader = ImageLoader()
    
    t = threading.Thread(target=socket_worker, args=(tracker,), daemon=True)
    t.start()

    pygame.init()
    WIDTH, HEIGHT = 900, 360
    screen = pygame.display.set_mode((WIDTH, HEIGHT))
    pygame.display.set_caption("FlightScanr Dashboard")
    clock = pygame.time.Clock()

    # Schriften (Clean Sans-Serif / Monospace)
    font_bold_large = pygame.font.SysFont("arial", 22, bold=True)
    font_medium = pygame.font.SysFont("arial", 13, bold=True)
    font_dim = pygame.font.SysFont("monospace", 12)
    font_data = pygame.font.SysFont("monospace", 14, bold=True)
    font_clock = pygame.font.SysFont("monospace", 12)

    running = True
    while running:
        for event in pygame.event.get():
            if event.type == pygame.QUIT or (event.type == pygame.KEYDOWN and event.key == pygame.K_ESCAPE):
                running = False

        screen.fill(COLOR_BG)
        aircrafts = tracker.get_snapshot()

        # Nächstes Flugzeug als aktives Ziel auswählen
        target = None
        if aircrafts:
            valid_targets = [a for a in aircrafts.values() if a["dist_km"] > 0]
            if valid_targets:
                target = min(valid_targets, key=lambda x: x["dist_km"])

        # --- 1. LINKER BEREICH: TELEMETRIE (Typ, Airline, GS, ALT, HDG) ---
        x_left = 30
        if target:
            # Flugzeugtyp & Airline Oben Links
            screen.blit(font_bold_large.render(target["name"], True, COLOR_TEXT_MAIN), (x_left, 35))
            screen.blit(font_medium.render(target["operator"], True, COLOR_TEXT_DIM), (x_left, 65))
            screen.blit(font_dim.render(f"REG: {target['reg']} | CALL: {target['callsign']}", True, COLOR_TEXT_DIM), (x_left, 85))

            # Messwerte Unten Links (Exakt wie auf dem Beispielfoto)
            y_data = 170
            metrics = [
                ("GS:", f"{target['speed']} kt"),
                ("ALT:", f"{target['alt']} ft"),
                ("HDG:", f"{target['track']:.1f}°"),
                ("DIST:", f"{target['dist_km']:.1f} km")
            ]

            for label, val in metrics:
                screen.blit(font_dim.render(label, True, COLOR_TEXT_DIM), (x_left, y_data))
                screen.blit(font_data.render(val, True, COLOR_TEXT_MAIN), (x_left + 55, y_data - 1))
                y_data += 26
        else:
            screen.blit(font_bold_large.render("SEARCHING...", True, COLOR_TEXT_DIM), (x_left, 35))
            screen.blit(font_dim.render("Warte auf Transpondersignal", True, COLOR_TEXT_DIM), (x_left, 70))

        # --- 2. MITTLERER BEREICH: PASSEGENAUES FLUGZEUGBILD ---
        center_x = 450
        center_y = 180
        if target:
            ac_img = img_loader.get_image(target["type"], 280)
            if ac_img:
                rect = ac_img.get_rect(center=(center_x, center_y))
                screen.blit(ac_img, rect)

        # --- 3. RECHTER BEREICH: MINIMALISTISCHES RADAR ---
        rc_x = 760
        rc_y = 170
        r_radius = 100

        # Entfernungsringe & Fadenkreuz
        for r_km in [25, 50, 75, 100]:
            px_r = int((r_km / MAX_RANGE_KM) * r_radius)
            pygame.draw.circle(screen, COLOR_GRID, (rc_x, rc_y), px_r, 1)

        pygame.draw.line(screen, COLOR_GRID, (rc_x - r_radius, rc_y), (rc_x + r_radius, rc_y), 1)
        pygame.draw.line(screen, COLOR_GRID, (rc_x, rc_y - r_radius), (rc_x, rc_y + r_radius), 1)

        # Radar-Blips
        for icao_code, data in aircrafts.items():
            if data["dist_km"] > 0 and data["dist_km"] <= MAX_RANGE_KM:
                rad = math.radians(data["bearing"])
                dist_p = (data["dist_km"] / MAX_RANGE_KM) * r_radius
                
                bx = int(rc_x + dist_p * math.sin(rad))
                by = int(rc_y - dist_p * math.cos(rad))

                is_target = (target and data == target)
                radius = 4 if is_target else 2
                
                pygame.draw.circle(screen, COLOR_TEXT_MAIN if is_target else COLOR_TEXT_DIM, (bx, by), radius)

        # Live-Uhrzeit unten rechts (wie im Foto)
        now_str = datetime.datetime.now().strftime("%H:%M")
        clock_txt = font_clock.render(now_str, True, COLOR_TEXT_MAIN)
        screen.blit(clock_txt, (WIDTH - 50, HEIGHT - 30))

        pygame.display.flip()
        clock.tick(30)

    pygame.quit()
    sys.exit()

if __name__ == "__main__":
    main()