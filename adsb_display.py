#!/usr/bin/env python3
"""
ADS-B Radar Display – Testversion für Raspberry Pi 4 / Windows

Funktionen:
- DEMO-Modus mit simulierten Flugzeugen
- LIVE-Modus liest /run/readsb/aircraft.json
- Radaransicht mit Distanzringen, Himmelsrichtungen und Sweep
- Flugzeugliste mit Callsign, Entfernung, Höhe, Geschwindigkeit und Kurs
- Klick auf ein Flugzeug zeigt Detailinformationen
- Keine Datenbank notwendig: aktuelle Flugzeuge liegen nur im RAM

Tastatur:
    D = Demo / Live
    F = Vollbild
    +/- = Radarreichweite ändern
    ESC = Ende
"""

import json
import math
import os
import random
import time
import tkinter as tk
from tkinter import font

# ------------------------------------------------------------
# Einstellungen
# ------------------------------------------------------------

AIRCRAFT_FILE = "/run/readsb/aircraft.json"

# Später durch die exakten Koordinaten eurer Empfangsstation ersetzen.
STATION_LAT = 47.8095
STATION_LON = 13.0550

RADAR_RANGE_KM = 80.0
RANGE_STEPS = [20.0, 40.0, 60.0, 80.0, 120.0, 160.0]
FRAME_MS = 33
DATA_UPDATE_MS = 500
MAX_LIST_PLANES = 11

# ------------------------------------------------------------
# Farben / Hilfsfunktionen
# ------------------------------------------------------------

BG = "#020703"
PANEL_BG = "#051109"
GRID = "#125d31"
GRID_DARK = "#0a331c"
GREEN = "#20ff75"
GREEN_DIM = "#75c895"
WHITE = "#eafff0"
YELLOW = "#ffe66d"
RED = "#ff6666"
CYAN = "#74e8ff"


def haversine_km(lat1, lon1, lat2, lon2):
    """Entfernung zwischen zwei GPS-Punkten in km."""
    r = 6371.0
    p1 = math.radians(lat1)
    p2 = math.radians(lat2)
    dp = math.radians(lat2 - lat1)
    dl = math.radians(lon2 - lon1)

    a = math.sin(dp / 2) ** 2 + math.cos(p1) * math.cos(p2) * math.sin(dl / 2) ** 2
    return 2 * r * math.asin(math.sqrt(a))


def bearing_deg(lat1, lon1, lat2, lon2):
    """Richtung von Punkt 1 nach Punkt 2 in Grad."""
    p1 = math.radians(lat1)
    p2 = math.radians(lat2)
    dl = math.radians(lon2 - lon1)

    x = math.sin(dl) * math.cos(p2)
    y = math.cos(p1) * math.sin(p2) - math.sin(p1) * math.cos(p2) * math.cos(dl)
    return (math.degrees(math.atan2(x, y)) + 360) % 360


def clean_callsign(value):
    if value is None:
        return "--------"
    value = str(value).strip().upper()
    return value[:8] or "--------"


def clean_hex(value):
    if value is None:
        return "------"
    return str(value).strip().upper()[:6]


def format_altitude(value):
    if value is None:
        return "----"
    try:
        return f"{int(float(value)):,}".replace(",", ".")
    except (ValueError, TypeError):
        return "----"


def format_speed(value):
    if value is None:
        return "---"
    try:
        return f"{int(float(value))}"
    except (ValueError, TypeError):
        return "---"


def format_track(value):
    if value is None:
        return "---"
    try:
        return f"{float(value):.0f}°"
    except (ValueError, TypeError):
        return "---"


def short_direction(deg):
    """Gradwert -> grobe Himmelsrichtung."""
    if deg is None:
        return "---"
    try:
        dirs = ["N", "NE", "E", "SE", "S", "SW", "W", "NW"]
        index = int((float(deg) + 22.5) // 45) % 8
        return dirs[index]
    except (ValueError, TypeError):
        return "---"

# ------------------------------------------------------------
# Demo-Daten
# ------------------------------------------------------------

DEMO_AIRCRAFT = [
    {"hex": "3C4AAA", "flight": "DLH123", "lat": 47.95, "lon": 13.25, "altitude_baro": 32000, "gs": 450, "track": 275},
    {"hex": "4CA111", "flight": "RYR45", "lat": 47.70, "lon": 12.80, "altitude_baro": 18000, "gs": 420, "track": 95},
    {"hex": "4402BB", "flight": "AUA62", "lat": 48.00, "lon": 13.00, "altitude_baro": 24000, "gs": 390, "track": 180},
    {"hex": "471234", "flight": "WZZ81", "lat": 47.65, "lon": 13.35, "altitude_baro": 14500, "gs": 370, "track": 35},
    {"hex": "495211", "flight": "TAP72", "lat": 47.58, "lon": 12.98, "altitude_baro": 21000, "gs": 405, "track": 320},
    {"hex": "406789", "flight": "BAW91", "lat": 47.86, "lon": 13.42, "altitude_baro": 28000, "gs": 430, "track": 65},
    {"hex": "4B1234", "flight": "SWR34", "lat": 48.15, "lon": 13.28, "altitude_baro": 36000, "gs": 470, "track": 210},
    {"hex": "3D7788", "flight": "EZY18", "lat": 47.47, "lon": 13.18, "altitude_baro": 12000, "gs": 340, "track": 15},
    {"hex": "4A5566", "flight": "UAE50", "lat": 48.02, "lon": 12.70, "altitude_baro": 39000, "gs": 465, "track": 110},
]


def get_demo_aircraft():
    """Erzeugt leicht bewegte Demo-Flugzeuge."""
    now = time.time()
    result = []

    for i, original in enumerate(DEMO_AIRCRAFT):
        plane = dict(original)
        phase = now * (0.35 + i * 0.025) + i
        plane["lat"] += math.sin(phase) * 0.018
        plane["lon"] += math.cos(phase * 0.92) * 0.025
        result.append(plane)

    return result

# ------------------------------------------------------------
# ADS-B Daten lesen
# ------------------------------------------------------------


def load_aircraft_from_readsb():
    """Liest die von readsb erzeugte aircraft.json."""
    if not os.path.exists(AIRCRAFT_FILE):
        return []

    try:
        with open(AIRCRAFT_FILE, "r", encoding="utf-8") as file:
            data = json.load(file)
    except (OSError, json.JSONDecodeError):
        return []

    result = []
    for plane in data.get("aircraft", []):
        lat = plane.get("lat")
        lon = plane.get("lon")
        if lat is None or lon is None:
            continue

        try:
            plane = dict(plane)
            plane["lat"] = float(lat)
            plane["lon"] = float(lon)
        except (ValueError, TypeError):
            continue

        result.append(plane)

    return result

# ------------------------------------------------------------
# Hauptanwendung
# ------------------------------------------------------------


class RadarApp:
    def __init__(self, root):
        self.root = root
        self.root.title("ADS-B Ground Station")
        self.root.configure(bg=BG)
        self.root.minsize(1000, 650)

        self.demo_mode = True
        self.fullscreen = False
        self.selected_hex = None
        self.aircraft = []
        self.radar_range_km = RADAR_RANGE_KM
        self.sweep_angle = 0.0
        self.last_update = time.perf_counter()
        self.last_data_update = 0.0
        self.sweep_speed = 75.0  # Grad pro Sekunde

        # Schriftarten
        self.title_font = font.Font(family="DejaVu Sans", size=22, weight="bold")
        self.section_font = font.Font(family="DejaVu Sans", size=13, weight="bold")
        self.big_font = font.Font(family="DejaVu Sans", size=17, weight="bold")
        self.small_font = font.Font(family="DejaVu Sans", size=10)
        self.mono_font = font.Font(family="DejaVu Sans Mono", size=10)
        self.mono_bold = font.Font(family="DejaVu Sans Mono", size=11, weight="bold")

        self.canvas = tk.Canvas(root, bg=BG, highlightthickness=0)
        self.canvas.pack(fill="both", expand=True)

        self.root.bind("<Escape>", lambda event: self.root.destroy())
        self.root.bind("<f>", lambda event: self.toggle_fullscreen())
        self.root.bind("<F>", lambda event: self.toggle_fullscreen())
        self.root.bind("<d>", lambda event: self.toggle_demo())
        self.root.bind("<D>", lambda event: self.toggle_demo())
        self.root.bind("<plus>", lambda event: self.change_range(1))
        self.root.bind("<KP_Add>", lambda event: self.change_range(1))
        self.root.bind("<minus>", lambda event: self.change_range(-1))
        self.root.bind("<KP_Subtract>", lambda event: self.change_range(-1))
        self.canvas.bind("<Button-1>", self.mouse_click)

        self.update_screen()

    # --------------------------------------------------------
    # Anzeige
    # --------------------------------------------------------

    def draw_header(self, width):
        self.canvas.create_rectangle(0, 0, width, 82, fill="#030a05", outline=GRID_DARK)

        self.canvas.create_text(
            24, 17,
            text="ADS-B GROUND STATION",
            fill=GREEN,
            font=self.title_font,
            anchor="nw",
        )
        self.canvas.create_text(
            26, 53,
            text="1090 MHz  |  LOCAL AIRCRAFT TRACKING",
            fill=GREEN_DIM,
            font=self.small_font,
            anchor="nw",
        )

        status = "DEMO DATA" if self.demo_mode else "READSB LIVE"
        status_color = YELLOW if self.demo_mode else GREEN
        self.canvas.create_text(
            width - 28, 20,
            text=status,
            fill=status_color,
            font=self.section_font,
            anchor="ne",
        )

        self.canvas.create_text(
            width - 28, 49,
            text=f"RANGE {self.radar_range_km:.0f} km   |   D / F / +/-",
            fill=GREEN_DIM,
            font=self.small_font,
            anchor="ne",
        )

    def draw_radar(self, x0, y0, x1, y1):
        center_x = (x0 + x1) / 2
        center_y = (y0 + y1) / 2
        available_w = x1 - x0
        available_h = y1 - y0
        radius = min(available_w, available_h) * 0.43

        # Radarfläche
        self.canvas.create_oval(
            center_x - radius,
            center_y - radius,
            center_x + radius,
            center_y + radius,
            fill="#031208",
            outline=GRID,
            width=2,
        )

        # Distanzringe
        for fraction in (0.25, 0.50, 0.75, 1.00):
            r = radius * fraction
            self.canvas.create_oval(
                center_x - r,
                center_y - r,
                center_x + r,
                center_y + r,
                outline=GRID,
                width=1,
            )
            km = self.radar_range_km * fraction
            self.canvas.create_text(
                center_x + 7,
                center_y - r - 2,
                text=f"{km:.0f} km",
                fill=GREEN_DIM,
                font=self.small_font,
                anchor="s",
            )

        # 45° Hilfslinien
        for angle_deg in range(0, 360, 45):
            angle = math.radians(angle_deg)
            ex = center_x + math.sin(angle) * radius
            ey = center_y - math.cos(angle) * radius
            self.canvas.create_line(
                center_x, center_y, ex, ey,
                fill=GRID_DARK,
                width=1,
            )

        # Himmelsrichtungen
        directions = [("N", 0), ("NE", 45), ("E", 90), ("SE", 135),
                      ("S", 180), ("SW", 225), ("W", 270), ("NW", 315)]
        for label, deg in directions:
            angle = math.radians(deg)
            rr = radius + 23
            tx = center_x + math.sin(angle) * rr
            ty = center_y - math.cos(angle) * rr
            self.canvas.create_text(tx, ty, text=label, fill=GREEN, font=self.small_font)

        # Sweep-Linie
        sweep = math.radians(self.sweep_angle)
        sx = center_x + math.sin(sweep) * radius
        sy = center_y - math.cos(sweep) * radius
        self.canvas.create_line(center_x, center_y, sx, sy, fill=GREEN, width=2)

        # Flugzeugpunkte
        for plane in self.aircraft:
            lat = plane.get("lat")
            lon = plane.get("lon")
            if lat is None or lon is None:
                continue

            distance = haversine_km(STATION_LAT, STATION_LON, lat, lon)
            if distance > self.radar_range_km:
                continue

            bearing = bearing_deg(STATION_LAT, STATION_LON, lat, lon)
            angle = math.radians(bearing)
            scale = distance / self.radar_range_km
            px = center_x + math.sin(angle) * radius * scale
            py = center_y - math.cos(angle) * radius * scale

            selected = clean_hex(plane.get("hex")) == self.selected_hex
            point_color = YELLOW if selected else WHITE

            # Kleine Flugzeug-Markierung, grob in Flugrichtung ausgerichtet
            track = plane.get("track")
            try:
                heading = math.radians(float(track))
            except (ValueError, TypeError):
                heading = angle

            dx = math.sin(heading)
            dy = -math.cos(heading)
            side_x = -dy
            side_y = dx
            length = 9 if selected else 7
            wing = 5 if selected else 4

            nose = (px + dx * length, py + dy * length)
            left = (px - dx * length * 0.45 + side_x * wing, py - dy * length * 0.45 + side_y * wing)
            tail = (px - dx * length * 0.6, py - dy * length * 0.6)
            right = (px - dx * length * 0.45 - side_x * wing, py - dy * length * 0.45 - side_y * wing)

            self.canvas.create_polygon(
                nose, left, tail, right,
                fill=point_color,
                outline=point_color,
            )

            callsign = clean_callsign(plane.get("flight"))
            altitude = format_altitude(plane.get("altitude_baro"))
            label = f"{callsign}  {altitude} ft"

            self.canvas.create_text(
                px + 11,
                py - 10,
                text=label,
                fill=point_color,
                font=self.small_font,
                anchor="w",
            )

        # Stationssymbol
        self.canvas.create_oval(
            center_x - 5, center_y - 5,
            center_x + 5, center_y + 5,
            fill=GREEN,
            outline=WHITE,
        )
        self.canvas.create_text(
            center_x + 12,
            center_y + 12,
            text="STATION",
            fill=GREEN,
            font=self.small_font,
            anchor="nw",
        )

        return center_x, center_y, radius

    def visible_planes(self):
        planes = []
        for plane in self.aircraft:
            lat = plane.get("lat")
            lon = plane.get("lon")
            if lat is None or lon is None:
                continue

            distance = haversine_km(STATION_LAT, STATION_LON, lat, lon)
            if distance <= self.radar_range_km:
                copy = dict(plane)
                copy["distance"] = distance
                copy["bearing"] = bearing_deg(STATION_LAT, STATION_LON, lat, lon)
                planes.append(copy)

        planes.sort(key=lambda p: p.get("distance", 9999))
        return planes

    def draw_side_panel(self, width, height):
        panel_x = width * 0.72
        self.canvas.create_rectangle(panel_x, 82, width, height, fill=PANEL_BG, outline="")
        self.canvas.create_line(panel_x, 82, panel_x, height, fill=GRID, width=1)

        self.canvas.create_text(
            panel_x + 20, 105,
            text="TRAFFIC",
            fill=GREEN,
            font=self.section_font,
            anchor="nw",
        )

        planes = self.visible_planes()
        y = 142
        row_h = 59

        for index, plane in enumerate(planes[:MAX_LIST_PLANES]):
            callsign = clean_callsign(plane.get("flight"))
            hex_code = clean_hex(plane.get("hex"))
            distance = plane.get("distance", 0.0)
            altitude = format_altitude(plane.get("altitude_baro"))
            speed = format_speed(plane.get("gs"))
            track = format_track(plane.get("track"))
            selected = hex_code == self.selected_hex

            if selected:
                self.canvas.create_rectangle(
                    panel_x + 10, y - 7, width - 12, y + row_h - 7,
                    fill="#0b2113", outline=GREEN
                )

            self.canvas.create_text(
                panel_x + 20,
                y,
                text=f"{index + 1:02d}  {callsign:<8}",
                fill=YELLOW if selected else WHITE,
                font=self.mono_bold,
                anchor="nw",
            )
            self.canvas.create_text(
                width - 20,
                y,
                text=f"{distance:5.1f} km",
                fill=GREEN_DIM,
                font=self.mono_font,
                anchor="ne",
            )
            self.canvas.create_text(
                panel_x + 20,
                y + 21,
                text=f"ALT {altitude:>6} ft   SPD {speed:>3} kt",
                fill=GREEN_DIM,
                font=self.mono_font,
                anchor="nw",
            )
            self.canvas.create_text(
                panel_x + 20,
                y + 39,
                text=f"HDG {track:>4}   {short_direction(plane.get('track')):>2}   ICAO {hex_code}",
                fill=GREEN_DIM,
                font=self.mono_font,
                anchor="nw",
            )

            y += row_h

        # Detailbox
        detail_y = min(height - 175, y + 5)
        self.canvas.create_line(panel_x + 15, detail_y, width - 15, detail_y, fill=GRID)
        self.canvas.create_text(
            panel_x + 20,
            detail_y + 12,
            text="AUSWAHL",
            fill=GREEN,
            font=self.small_font,
            anchor="nw",
        )

        selected_plane = None
        for plane in planes:
            if clean_hex(plane.get("hex")) == self.selected_hex:
                selected_plane = plane
                break

        if selected_plane:
            self.draw_selected_info(panel_x + 20, detail_y + 34, selected_plane)
        else:
            self.canvas.create_text(
                panel_x + 20,
                detail_y + 34,
                text="Flugzeug anklicken",
                fill=GREEN_DIM,
                font=self.small_font,
                anchor="nw",
            )

        self.canvas.create_text(
            panel_x + 20,
            height - 55,
            text=f"TRACKS {len(planes)}   |   RANGE ±  = {self.radar_range_km:.0f} km",
            fill=GREEN,
            font=self.mono_font,
            anchor="sw",
        )
        self.canvas.create_text(
            panel_x + 20,
            height - 30,
            text="D Demo/Live    F Vollbild    ESC Ende    Klick = Auswahl",
            fill=GREEN_DIM,
            font=self.small_font,
            anchor="sw",
        )

    def draw_selected_info(self, x, y, plane):
        callsign = clean_callsign(plane.get("flight"))
        self.canvas.create_text(x, y, text=callsign, fill=YELLOW, font=self.big_font, anchor="nw")
        lines = [
            f"ICAO {clean_hex(plane.get('hex'))}",
            f"LAT {plane.get('lat', 0):.5f}   LON {plane.get('lon', 0):.5f}",
            f"DIST {plane.get('distance', 0):.1f} km   BRG {plane.get('bearing', 0):.0f}°",
        ]
        for index, line in enumerate(lines):
            self.canvas.create_text(x, y + 28 + index * 17, text=line, fill=GREEN_DIM, font=self.mono_font, anchor="nw")

    # --------------------------------------------------------
    # Interaktion
    # --------------------------------------------------------

    def toggle_demo(self):
        self.demo_mode = not self.demo_mode
        self.selected_hex = None

    def toggle_fullscreen(self):
        self.fullscreen = not self.fullscreen
        self.root.attributes("-fullscreen", self.fullscreen)

    def change_range(self, direction):
        current_index = min(range(len(RANGE_STEPS)), key=lambda i: abs(RANGE_STEPS[i] - self.radar_range_km))
        new_index = max(0, min(len(RANGE_STEPS) - 1, current_index + direction))
        self.radar_range_km = RANGE_STEPS[new_index]

    def mouse_click(self, event):
        """Wählt ein Flugzeug durch Klick in Radar oder Liste."""
        planes = self.visible_planes()
        if not planes:
            return

        width = self.canvas.winfo_width()
        height = self.canvas.winfo_height()
        center_x = width * 0.36
        center_y = height * 0.53
        radius = min(width * 0.60, height * 0.82) * 0.43

        # Falls Fenstergröße stark verändert wurde, robuster durch Neuberechnung.
        x_limit = width * 0.70
        y_top = 82
        y_bottom = height
        center_x = x_limit * 0.5
        center_y = (y_top + y_bottom) * 0.5
        radius = min(x_limit, height - y_top) * 0.43

        closest = None
        closest_distance = 18.0

        for plane in planes:
            bearing = plane.get("bearing", 0)
            distance = plane.get("distance", 0)
            angle = math.radians(bearing)
            px = center_x + math.sin(angle) * radius * (distance / self.radar_range_km)
            py = center_y - math.cos(angle) * radius * (distance / self.radar_range_km)
            screen_distance = math.hypot(event.x - px, event.y - py)
            if screen_distance < closest_distance:
                closest_distance = screen_distance
                closest = plane

        if closest:
            self.selected_hex = clean_hex(closest.get("hex"))

    # --------------------------------------------------------
    # Aktualisierung
    # --------------------------------------------------------

    def update_screen(self):
        width = self.canvas.winfo_width()
        height = self.canvas.winfo_height()

        if width < 500 or height < 350:
            self.root.after(FRAME_MS, self.update_screen)
            return

        now = time.perf_counter()
        dt = min(now - self.last_update, 0.1)
        self.last_update = now

        # Daten deutlich seltener laden als die Grafik zeichnen.
        # Dadurch bleibt die Animation flüssig und JSON wird nicht unnötig oft gelesen.
        if now - self.last_data_update >= DATA_UPDATE_MS / 1000.0:
            if self.demo_mode:
                self.aircraft = get_demo_aircraft()
            else:
                self.aircraft = load_aircraft_from_readsb()
            self.last_data_update = now

        # Flüssiger Radar-Sweep mit zeitbasierter Geschwindigkeit.
        self.sweep_angle = (self.sweep_angle + self.sweep_speed * dt) % 360.0

        self.canvas.delete("all")
        self.draw_header(width)
        self.draw_radar(20, 90, width * 0.70 - 10, height - 15)
        self.draw_side_panel(width, height)

        self.root.after(FRAME_MS, self.update_screen)


def main():
    root = tk.Tk()
    RadarApp(root)
    root.mainloop()


if __name__ == "__main__":
    main()
