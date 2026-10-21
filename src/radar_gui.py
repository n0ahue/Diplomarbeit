import socket
import time
import math
import threading
import datetime
import os
import pygame
import sys

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

            # MSG 1: Transponder Rufzeichen (Callsign)
            if msg_type == "1":
                call = parts[10].strip()
                if call:
                    self.aircrafts[icao_hex]["callsign"] = call
                    info = resolve_aircraft_type_from_transponder(icao_hex, call)
                    self.aircrafts[icao_hex].update(info)

            # MSG 3: Position & Höhe
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

            # MSG 4: Geschwindigkeit & Kurs
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
# 4. BILDER-LOADER (EXAKTE ZUORDNUNG ZU DEINEN B