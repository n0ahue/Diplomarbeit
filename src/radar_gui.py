import socket
import time
import math
import threading
import datetime
import pygame
import sys

# --- KONFIGURATION ---
PI_IP = "127.0.0.1"       # Am Pi "127.0.0.1", am Laptop die IP des Pi
PORT = 30003
TIMEOUT_SECONDS = 60

HOME_LAT = 47.82194
HOME_LON = 13.04569
MAX_RANGE_KM = 100.0      # Radar-Reichweite in km

# Farben (Dark Minimalist Theme)
COLOR_BG = (18, 20, 26)
COLOR_PANEL_BORDER = (40, 48, 60)
COLOR_GRID = (60, 80, 100)
COLOR_TEXT_MAIN = (240, 245, 250)
COLOR_TEXT_DIM = (140, 155, 175)
COLOR_ACCENT = (0, 220, 180)       # Cyan/Grün Glow
COLOR_RADAR_BLIP = (0, 255, 190)

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

def draw_aircraft_silhouette(surface, rect):
    """Zieht eine Vektor-Silhouette eines Verkehrsflugzeugs (Seitenansicht)"""
    cx = rect.centerx
    cy = rect.centery + 10
    w = rect.width * 0.75
    h = rect.height * 0.4

    # Relativ koordinierte Vektor-Punkte für die Flugzeug-Silhouette
    pts_norm = [
        (-0.50, 0.02),   # Bug Spitze
        (-0.45, -0.06),  # Cockpit Scheibe
        (-0.35, -0.08),  # Rumpf oben
        (0.20, -0.08),   # Rumpf hinten
        (0.35, -0.38),   # Leitwerk Spitze oben
        (0.45, -0.38),   # Leitwerk oben hinten
        (0.48, -0.02),   # Heckkonus
        (0.40, 0.05),    # Rumpf unten hinten
        (0.05, 0.08),    # Flügelansatz
        (-0.10, 0.28),   # Flügel Spitze unten
        (-0.20, 0.28),   # Flügel Vorne
        (-0.08, 0.08),   # Triebwerk Aufhängung
        (-0.35, 0.06),   # Rumpf unten vorne
        (-0.48, 0.04)    # Bug unten
    ]

    pts = [(int(cx + x * w), int(cy + y * h)) for x, y in pts_norm]
    pygame.draw.polygon(surface, COLOR_TEXT_MAIN, pts, 2)

def main():
    tracker = AircraftTracker()
    
    t = threading.Thread(target=socket_worker, args=(tracker,), daemon=True)
    t.start()

    pygame.init()
    # Breites Widescreen-Layout (z.B. 1024 x 400)
    WIDTH, HEIGHT = 1024, 400
    screen = pygame.display.set_mode((WIDTH, HEIGHT))
    pygame.display.set_caption("FlightScanr - Desktop Radar")
    clock = pygame.time.Clock()

    font_title = pygame.font.SysFont("monospace", 18, bold=True)
    font_sub = pygame.font.SysFont("monospace", 12)
    font_data = pygame.font.SysFont("monospace", 15, bold=True)
    font_clock = pygame.font.SysFont("monospace", 13)

    # Panel-Aufteilung
    left_rect = pygame.Rect(20, 20, 320, 360)
    center_rect = pygame.Rect(350, 20, 340, 360)
    right_rect = pygame.Rect(700, 20, 304, 360)

    running = True
    while running:
        for event in pygame.event.get():
            if event.type == pygame.QUIT or (event.type == pygame.KEYDOWN and event.key == pygame.K_ESCAPE):
                running = False

        screen.fill(COLOR_BG)

        aircrafts = tracker.get_snapshot()

        # Nächstes / Aktives Flugzeug für das Hauptfenster auswählen
        target = None
        if aircrafts:
            # Wähle das Flugzeug mit der geringsten Entfernung
            valid_targets = [a for a in aircrafts.values() if a["dist_km"] > 0]
            if valid_targets:
                target = min(valid_targets, key=lambda x: x["dist_km"])

        # --- 1. LINKES PANEL: TELEMETRIE & FLUGDATEN ---
        if target:
            callsign = target["callsign"] if target["callsign"] != "N/A" else "UNKNOWN"
            icao = list(aircrafts.keys())[list(aircrafts.values()).index(target)]

            # Titelzeilen
            screen.blit(font_title.render(f"FLIGHT {callsign}", True, COLOR_TEXT_MAIN), (left_rect.x, left_rect.y + 10))
            screen.blit(font_sub.render(f"ICAO: {icao} | COMMERCIAL AIRCRAFT", True, COLOR_TEXT_DIM), (left_rect.x, left_rect.y + 35))

            # Trennlinie
            pygame.draw.line(screen, COLOR_PANEL_BORDER, (left_rect.x, left_rect.y + 60), (left_rect.right, left_rect.y + 60), 1)

            # Datenwerte im Grid
            y_off = left_rect.y + 80
            metrics = [
                ("GS (SPEED):", f"{target['speed']} kt"),
                ("ALTITUDE:", f"{target['alt']} ft"),
                ("HEADING:", f"{target['track']:.1f}°"),
                ("DISTANCE:", f"{target['dist_km']:.1f} km"),
                ("BEARING:", f"{target['bearing']:.0f}°"),
                ("LAT / LON:", f"{target['lat']:.3f}, {target['lon']:.3f}")
            ]

            for label, val in metrics:
                screen.blit(font_sub.render(label, True, COLOR_TEXT_DIM), (left_rect.x, y_off))
                screen.blit(font_data.render(val, True, COLOR_ACCENT), (left_rect.x + 130, y_off - 2))
                y_off += 38
        else:
            screen.blit(font_title.render("SEARCHING TARGET...", True, COLOR_TEXT_DIM), (left_rect.x, left_rect.y + 20))
            screen.blit(font_sub.render("Warte auf ADS-B Transponder...", True, COLOR_TEXT_DIM), (left_rect.x, left_rect.y + 50))

        # --- 2. MITTLERES PANEL: FLUGZEUG SILHOUETTE ---
        draw_aircraft_silhouette(screen, center_rect)
        if target:
            model_txt = font_sub.render("PASSENGER JET (GENERIC)", True, COLOR_TEXT_DIM)
            screen.blit(model_txt, (center_rect.centerx - model_txt.get_width() // 2, center_rect.bottom - 40))

        # --- 3. RECHTES PANEL: KREISFÖRMIGES RADAR ---
        rc_x = right_rect.centerx
        rc_y = right_rect.centery - 10
        r_radius = 120

        # Entfernungsringe
        for r_km in [25, 50, 75, 100]:
            px_r = int((r_km / MAX_RANGE_KM) * r_radius)
            pygame.draw.circle(screen, COLOR_GRID, (rc_x, rc_y), px_r, 1)

        # Fadenkreuz
        pygame.draw.line(screen, COLOR_GRID, (rc_x - r_radius, rc_y), (rc_x + r_radius, rc_y), 1)
        pygame.draw.line(screen, COLOR_GRID, (rc_x, rc_y - r_radius), (rc_x, rc_y + r_radius), 1)

        # Flugzeuge als Radarblips plotten
        for icao_code, data in aircrafts.items():
            if data["dist_km"] > 0 and data["dist_km"] <= MAX_RANGE_KM:
                rad = math.radians(data["bearing"])
                dist_p = (data["dist_km"] / MAX_RANGE_KM) * r_radius
                
                bx = int(rc_x + dist_p * math.sin(rad))
                by = int(rc_y - dist_p * math.cos(rad))

                # Aktives Ziel besonders hervorheben
                is_target = (target and data == target)
                color = COLOR_ACCENT if is_target else COLOR_RADAR_BLIP
                radius = 5 if is_target else 3
                
                pygame.draw.circle(screen, color, (bx, by), radius)

                # Kursanzeige-Vektor
                if data["track"] > 0:
                    t_rad = math.radians(data["track"])
                    vx = int(bx + 12 * math.sin(t_rad))
                    vy = int(by - 12 * math.cos(t_rad))
                    pygame.draw.line(screen, color, (bx, by), (vx, vy), 1)

        # Live-Uhrzeit unten rechts (wie im Beispielfoto)
        now_str = datetime.datetime.now().strftime("%H:%M:%S")
        clock_txt = font_clock.render(now_str, True, COLOR_TEXT_DIM)
        screen.blit(clock_txt, (right_rect.right - clock_txt.get_width() - 10, right_rect.bottom - 25))

        pygame.display.flip()
        clock.tick(30)

    pygame.quit()
    sys.exit()

if __name__ == "__main__":
    main()