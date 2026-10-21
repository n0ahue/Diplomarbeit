import socket
import time
import math
import threading
import datetime
import os
import pygame
import sys
import traceback

# ==============================================================================
# 1. KONFIGURATION & THEME
# ==============================================================================
PI_IP = "127.0.0.1"       # Am Pi "127.0.0.1", am Laptop die IP des Pi
PORT = 30003
TIMEOUT_SECONDS = 45      # Inaktive Flugzeuge nach 45s entfernen

# Standort Empfänger (HTBLuVA Salzburg)
HOME_LAT = 47.82194
HOME_LON = 13.04569
MAX_RANGE_KM = 100.0      # Maximaler Radar-Radius in km

# Farbschema (Minimalist Dark Theme)
COLOR_BG = (15, 16, 18)           # Graphit-Schwarz
COLOR_TEXT_MAIN = (255, 255, 255)  # Reinweiß
COLOR_TEXT_DIM = (130, 135, 145)  # Dezentes Grau
COLOR_GRID = (40, 45, 55)         # Feines Radar-Raster
COLOR_RADAR_BLIP = (160, 175, 195)# Inaktive Radar-Punkte
COLOR_SELECTED = (0, 220, 180)     # Cyan-Highlight für ausgewähltes Ziel


# ==============================================================================
# 2. MATHEMATIK & TRANSPONDER-ERKENNUNG
# ==============================================================================
def calc_distance_and_bearing(lat1, lon1, lat2, lon2):
    """Berechnet Entfernung in km und Peilung (Azimut) in Grad von der Station aus"""
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


def resolve_aircraft_type_from_transponder(icao_hex, callsign):
    """Ermittelt Flugzeugtyp & Airline aus den empfangenen Transponderdaten"""
    icao_hex = icao_hex.upper().strip()
    callsign = callsign.upper().strip()

    ac_type = "GENERIC"
    name = "PASSENGER AIRCRAFT"
    operator = "COMMERCIAL FLIGHT"

    # Zuordnung anhand des Transponder-Rufzeichens (Callsign)
    if any(callsign.startswith(p) for p in ["EWG", "EWD", "DLH", "AUA", "EJU", "WZZ", "SWR", "LDM"]):
        ac_type = "A320"
        name = "AIRBUS A320-FAMILY"
        if callsign.startswith("EWG") or callsign.startswith("EWD"): operator = "EUROWINGS"
        elif callsign.startswith("DLH"): operator = "LUFTHANSA"
        elif callsign.startswith("AUA"): operator = "AUSTRIAN AIRLINES"
        elif callsign.startswith("EJU"): operator = "EASYJET"
        elif callsign.startswith("WZZ"): operator = "WIZZAIR"
        elif callsign.startswith("SWR"): operator = "SWISS INT. AIRLINES"

    elif any(callsign.startswith(p) for p in ["RYR", "MAY", "TUI", "EXS", "TRA", "DAL"]):
        ac_type = "B737"
        name = "BOEING 737-800"
        if callsign.startswith("RYR") or callsign.startswith("MAY"): operator = "RYANAIR"
        elif callsign.startswith("TUI"): operator = "TUI FLY"
        elif callsign.startswith("EXS"): operator = "JET2"

    return {
        "type": ac_type,
        "name": name,
        "operator": operator,
        "reg": icao_hex
    }


# ==============================================================================
# 3. DATEN-HANDLING & SOCKET LISTENER
# ==============================================================================
class AircraftTracker:
    def __init__(self):
        self.aircrafts = {}
        self.lock = threading.Lock()

    def process_sbs_line(self, line):
        """Zerlegt eine SBS-Textzeile vom readsb Socket auf Port 30003"""
        parts = line.strip().split(",")
        if len(parts) < 16:
            return

        msg_type = parts[1]
        icao_hex = parts[4].upper()
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
                    "type": "GENERIC",
                    "name": "SEARCHING...",
                    "operator": "COMMERCIAL AIRLINE",
                    "reg": icao_hex,
                    "last_seen": now
                }

            self.aircrafts[icao_hex]["last_seen"] = now

            if msg_type == "1":
                call = parts[10].strip()
                if call:
                    self.aircrafts[icao_hex]["callsign"] = call
                    info = resolve_aircraft_type_from_transponder(icao_hex, call)
                    self.aircrafts[icao_hex].update(info)

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

                    if self.aircrafts[icao_hex]["type"] == "GENERIC":
                        info = resolve_aircraft_type_from_transponder(icao_hex, self.aircrafts[icao_hex]["callsign"])
                        self.aircrafts[icao_hex].update(info)

            elif msg_type == "4":
                if len(parts) > 12 and parts[12]:
                    self.aircrafts[icao_hex]["speed"] = int(float(parts[12]))
                if len(parts) > 13 and parts[13]:
                    self.aircrafts[icao_hex]["track"] = float(parts[13])

    def cleanup(self):
        """Entfernt inaktive Flugzeuge nach Timeout"""
        now = time.time()
        with self.lock:
            expired = [icao for icao, data in self.aircrafts.items() if now - data["last_seen"] > TIMEOUT_SECONDS]
            for icao in expired:
                del self.aircrafts[icao]

    def get_snapshot(self):
        """Liefert eine sichere Kopie des Flugzeugspeichers für die GUI"""
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
                        tracker.process_sbs_line(line)
                except socket.timeout:
                    pass
                tracker.cleanup()
        except Exception:
            time.sleep(2.0)


# ==============================================================================
# 4. BILDER-LOADER (MIT CASING-MATCHING)
# ==============================================================================
class ImageLoader:
    def __init__(self):
        self.cache = {}

    def get_image(self, ac_type, target_width):
        ac_type = ac_type.upper().strip()

        if any(x in ac_type for x in ["A318", "A319", "A320", "A321", "A20N", "A21N"]):
            filename = "A320.png"
        elif any(x in ac_type for x in ["B73", "B737", "B738", "B739", "B38M", "BOEING737"]):
            filename = "Boeing737.png"
        else:
            filename = "Generic.png"

        if filename in self.cache:
            return self.cache[filename]

        filepath = f"assets/{filename}"
        if not os.path.exists(filepath):
            filepath = "assets/Generic.png"

        if os.path.exists(filepath):
            try:
                img = pygame.image.load(filepath).convert_alpha()
                aspect = img.get_height() / img.get_width()
                new_h = int(target_width * aspect)
                scaled = pygame.transform.smoothscale(img, (target_width, new_h))
                self.cache[filename] = scaled
                return scaled
            except Exception:
                pass

        self.cache[filename] = None
        return None


# ==============================================================================
# 5. HAUPT-PROGRAMM & PYGAME GUI LOOP
# ==============================================================================
def main():
    print("[DEBUG] Starte FlightScanr Radar GUI...")
    try:
        tracker = AircraftTracker()
        img_loader = ImageLoader()
        
        t = threading.Thread(target=socket_worker, args=(tracker,), daemon=True)
        t.start()
        print("[DEBUG] Socket-Empfangsthread gestartet.")

        pygame.init()
        WIDTH, HEIGHT = 900, 360
        screen = pygame.display.set_mode((WIDTH, HEIGHT))
        pygame.display.set_caption("FlightScanr - ADS-B Desktop Radar")
        clock = pygame.time.Clock()
        print("[DEBUG] Pygame Display-Fenster erzeugt.")

        font_bold_large = pygame.font.SysFont("arial", 22, bold=True)
        font_medium = pygame.font.SysFont("arial", 13, bold=True)
        font_dim = pygame.font.SysFont("monospace", 12)
        font_data = pygame.font.SysFont("monospace", 14, bold=True)
        font_clock = pygame.font.SysFont("monospace", 12)

        selected_icao = None
        auto_select = True
        last_target_switch = 0

        rc_x, rc_y = 760, 170
        r_radius = 100

        running = True
        frame_count = 0

        while running:
            frame_count += 1
            aircrafts = tracker.get_snapshot()
            aircraft_keys = list(aircrafts.keys())
            now = time.time()

            for event in pygame.event.get():
                if event.type == pygame.QUIT:
                    # Ignoriere Spurious QUIT Events in den ersten 10 Frames (Wayland Fix)
                    if frame_count > 10:
                        print("[DEBUG] Beenden per QUIT-Event gefordert.")
                        running = False
                elif event.type == pygame.KEYDOWN:
                    if event.key == pygame.K_ESCAPE:
                        print("[DEBUG] Beenden per ESC-Taste.")
                        running = False
                    elif aircraft_keys:
                        if event.key in [pygame.K_RIGHT, pygame.K_TAB]:
                            auto_select = False
                            if selected_icao not in aircraft_keys:
                                selected_icao = aircraft_keys[0]
                            else:
                                idx = (aircraft_keys.index(selected_icao) + 1) % len(aircraft_keys)
                                selected_icao = aircraft_keys[idx]

                        elif event.key == pygame.K_LEFT:
                            auto_select = False
                            if selected_icao not in aircraft_keys:
                                selected_icao = aircraft_keys[-1]
                            else:
                                idx = (aircraft_keys.index(selected_icao) - 1) % len(aircraft_keys)
                                selected_icao = aircraft_keys[idx]

                        elif event.key == pygame.K_a:
                            auto_select = True

                elif event.type == pygame.MOUSEBUTTONDOWN:
                    mx, my = event.pos
                    for icao_code, data in aircrafts.items():
                        if data["dist_km"] > 0 and data["dist_km"] <= MAX_RANGE_KM:
                            rad = math.radians(data["bearing"])
                            dist_p = (data["dist_km"] / MAX_RANGE_KM) * r_radius
                            bx = int(rc_x + dist_p * math.sin(rad))
                            by = int(rc_y - dist_p * math.cos(rad))

                            if math.hypot(mx - bx, my - by) <= 15:
                                selected_icao = icao_code
                                auto_select = False
                                break

            screen.fill(COLOR_BG)

            # Stabile Ziel-Auswahl
            target = None
            if aircrafts:
                valid_targets = [a for a in aircrafts.values() if a["dist_km"] > 0]
                if valid_targets:
                    closest = min(valid_targets, key=lambda x: x["dist_km"])
                    closest_icao = list(aircrafts.keys())[list(aircrafts.values()).index(closest)]

                    if auto_select:
                        if selected_icao not in aircrafts or (now - last_target_switch > 10.0 and closest_icao != selected_icao):
                            selected_icao = closest_icao
                            last_target_switch = now
                        target = aircrafts.get(selected_icao, closest)
                    else:
                        target = aircrafts.get(selected_icao, closest)

            # --- 1. TELEMETRIE ---
            x_left = 30
            if target:
                screen.blit(font_bold_large.render(target["name"], True, COLOR_TEXT_MAIN), (x_left, 35))
                screen.blit(font_medium.render(target["operator"], True, COLOR_TEXT_DIM), (x_left, 65))
                
                mode_str = "AUTO" if auto_select else "MANUAL"
                screen.blit(font_dim.render(f"REG: {target['reg']} | CALL: {target['callsign']} | MODE: {mode_str}", True, COLOR_TEXT_DIM), (x_left, 85))

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

            # --- 2. SILHOUETTE ---
            center_x = 450
            center_y = 180
            if target:
                ac_img = img_loader.get_image(target["type"], 280)
                if ac_img:
                    rect = ac_img.get_rect(center=(center_x, center_y))
                    screen.blit(ac_img, rect)

            # --- 3. RADAR ---
            for r_km in [25, 50, 75, 100]:
                px_r = int((r_km / MAX_RANGE_KM) * r_radius)
                pygame.draw.circle(screen, COLOR_GRID, (rc_x, rc_y), px_r, 1)

            pygame.draw.line(screen, COLOR_GRID, (rc_x - r_radius, rc_y), (rc_x + r_radius, rc_y), 1)
            pygame.draw.line(screen, COLOR_GRID, (rc_x, rc_y - r_radius), (rc_x, rc_y + r_radius), 1)

            for icao_code, data in aircrafts.items():
                if data["dist_km"] > 0 and data["dist_km"] <= MAX_RANGE_KM:
                    rad = math.radians(data["bearing"])
                    dist_p = (data["dist_km"] / MAX_RANGE_KM) * r_radius
                    
                    bx = int(rc_x + dist_p * math.sin(rad))
                    by = int(rc_y - dist_p * math.cos(rad))

                    is_selected = (icao_code == selected_icao)
                    color = COLOR_SELECTED if is_selected else COLOR_RADAR_BLIP
                    radius = 5 if is_selected else 2.5
                    
                    pygame.draw.circle(screen, color, (bx, by), radius)

            screen.blit(font_dim.render("[<- / ->] Switch | [A] Auto-Mode", True, COLOR_TEXT_DIM), (x_left, HEIGHT - 25))

            now_str = datetime.datetime.now().strftime("%H:%M")
            clock_txt = font_clock.render(now_str, True, COLOR_TEXT_MAIN)
            screen.blit(clock_txt, (WIDTH - 50, HEIGHT - 25))

            pygame.display.flip()
            clock.tick(30)

        pygame.quit()
        sys.exit()

    except Exception as e:
        print(f"\n[FEHLER IN MAIN]: {e}")
        traceback.print_exc()

if __name__ == "__main__":
    main()