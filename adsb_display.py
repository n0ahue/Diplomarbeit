#!/usr/bin/env python3
"""Clean ADS-B radar display – Demo + readsb live data."""

import json
import math
import os
import random
import time
import tkinter as tk
from tkinter import font

AIRCRAFT_FILE = "/run/readsb/aircraft.json"
STATION_LAT = 47.8095
STATION_LON = 13.0550

RADAR_RANGE_KM = 80.0
RANGE_STEPS = [20.0, 40.0, 60.0, 80.0, 120.0, 160.0]
FRAME_MS = 33
DATA_UPDATE_MS = 500
MAX_LIST_PLANES = 10

# Clean, modern dark UI
BG = "#0B0F14"
PANEL = "#11171E"
PANEL_2 = "#151D25"
BORDER = "#27313B"
GRID = "#26333D"
GRID_SOFT = "#1A242D"
TEXT = "#F2F5F7"
MUTED = "#8C99A5"
ACCENT = "#58C7FF"
ACCENT_SOFT = "#244D61"
WARNING = "#F5C451"

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


def haversine_km(lat1, lon1, lat2, lon2):
    r = 6371.0
    p1, p2 = math.radians(lat1), math.radians(lat2)
    dp = math.radians(lat2 - lat1)
    dl = math.radians(lon2 - lon1)
    a = math.sin(dp / 2) ** 2 + math.cos(p1) * math.cos(p2) * math.sin(dl / 2) ** 2
    return 2 * r * math.asin(math.sqrt(a))


def bearing_deg(lat1, lon1, lat2, lon2):
    p1, p2 = math.radians(lat1), math.radians(lat2)
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


def get_demo_aircraft():
    now = time.time()
    result = []
    for i, original in enumerate(DEMO_AIRCRAFT):
        plane = dict(original)
        phase = now * (0.35 + i * 0.025) + i
        plane["lat"] += math.sin(phase) * 0.018
        plane["lon"] += math.cos(phase * 0.92) * 0.025
        # Small slow altitude variation to make demo feel alive.
        plane["altitude_baro"] += int(math.sin(phase * 0.3) * 250)
        result.append(plane)
    return result


def load_aircraft_from_readsb():
    if not os.path.exists(AIRCRAFT_FILE):
        return []
    try:
        with open(AIRCRAFT_FILE, "r", encoding="utf-8") as file:
            data = json.load(file)
    except (OSError, json.JSONDecodeError):
        return []

    result = []
    for item in data.get("aircraft", []):
        lat, lon = item.get("lat"), item.get("lon")
        if lat is None or lon is None:
            continue
        try:
            plane = dict(item)
            plane["lat"] = float(lat)
            plane["lon"] = float(lon)
            result.append(plane)
        except (ValueError, TypeError):
            continue
    return result


class RadarApp:
    def __init__(self, root):
        self.root = root
        self.root.title("ADS-B Ground Station")
        self.root.configure(bg=BG)
        self.root.minsize(1100, 700)

        self.demo_mode = True
        self.fullscreen = False
        self.selected_hex = None
        self.aircraft = []
        self.radar_range_km = RADAR_RANGE_KM
        self.sweep_angle = 0.0
        self.last_update = time.perf_counter()
        self.last_data_update = 0.0
        self.sweep_speed = 42.0

        self.title_font = font.Font(family="DejaVu Sans", size=16, weight="bold")
        self.section_font = font.Font(family="DejaVu Sans", size=11, weight="bold")
        self.plane_font = font.Font(family="DejaVu Sans", size=11, weight="bold")
        self.data_font = font.Font(family="DejaVu Sans Mono", size=9)
        self.small_font = font.Font(family="DejaVu Sans", size=9)
        self.big_font = font.Font(family="DejaVu Sans", size=20, weight="bold")

        self.canvas = tk.Canvas(root, bg=BG, highlightthickness=0)
        self.canvas.pack(fill="both", expand=True)
        self.canvas.bind("<Button-1>", self.mouse_click)

        for key in ("<f>", "<F>"):
            self.root.bind(key, lambda event: self.toggle_fullscreen())
        for key in ("<d>", "<D>"):
            self.root.bind(key, lambda event: self.toggle_demo())
        for key in ("<plus>", "<KP_Add>"):
            self.root.bind(key, lambda event: self.change_range(1))
        for key in ("<minus>", "<KP_Subtract>"):
            self.root.bind(key, lambda event: self.change_range(-1))
        self.root.bind("<Escape>", lambda event: self.root.destroy())

        self.update_screen()

    def rounded_rect(self, x1, y1, x2, y2, radius=14, fill=PANEL, outline=None, width=1):
        # Canvas doesn't have rounded rectangles on all Tk versions, so use a smooth polygon/arc combination.
        self.canvas.create_rectangle(x1 + radius, y1, x2 - radius, y2, fill=fill, outline="")
        self.canvas.create_rectangle(x1, y1 + radius, x2, y2 - radius, fill=fill, outline="")
        self.canvas.create_oval(x1, y1, x1 + 2 * radius, y1 + 2 * radius, fill=fill, outline="")
        self.canvas.create_oval(x2 - 2 * radius, y1, x2, y1 + 2 * radius, fill=fill, outline="")
        self.canvas.create_oval(x1, y2 - 2 * radius, x1 + 2 * radius, y2, fill=fill, outline="")
        self.canvas.create_oval(x2 - 2 * radius, y2 - 2 * radius, x2, y2, fill=fill, outline="")
        if outline:
            self.canvas.create_rectangle(x1, y1, x2, y2, outline=outline, width=width)

    def draw_header(self, width):
        self.canvas.create_text(28, 25, text="ADS-B GROUND STATION", fill=TEXT, font=self.title_font, anchor="w")
        self.canvas.create_text(28, 49, text="1090 MHz  •  LOCAL TRAFFIC", fill=MUTED, font=self.small_font, anchor="w")

        status = "DEMO" if self.demo_mode else "LIVE"
        status_fill = WARNING if self.demo_mode else ACCENT
        self.rounded_rect(width - 170, 16, width - 28, 48, 12, fill=PANEL_2)
        self.canvas.create_oval(width - 154, 27, width - 146, 35, fill=status_fill, outline="")
        self.canvas.create_text(width - 137, 31, text=status, fill=TEXT, font=self.section_font, anchor="w")

        self.canvas.create_text(width - 28, 60, text=f"RANGE  {self.radar_range_km:.0f} km", fill=MUTED, font=self.small_font, anchor="e")
        self.canvas.create_line(24, 76, width - 24, 76, fill=BORDER)

    def visible_planes(self):
        planes = []
        for item in self.aircraft:
            lat, lon = item.get("lat"), item.get("lon")
            if lat is None or lon is None:
                continue
            distance = haversine_km(STATION_LAT, STATION_LON, lat, lon)
            if distance <= self.radar_range_km:
                plane = dict(item)
                plane["distance"] = distance
                plane["bearing"] = bearing_deg(STATION_LAT, STATION_LON, lat, lon)
                planes.append(plane)
        planes.sort(key=lambda p: p["distance"])
        return planes

    def draw_radar(self, x0, y0, x1, y1):
        cx = (x0 + x1) / 2
        cy = (y0 + y1) / 2 + 4
        radius = min((x1 - x0) * 0.43, (y1 - y0) * 0.43)

        self.rounded_rect(x0, y0, x1, y1, 18, fill=PANEL, outline=BORDER)
        self.canvas.create_text(x0 + 20, y0 + 18, text="RADAR", fill=TEXT, font=self.section_font, anchor="nw")
        self.canvas.create_text(x0 + 20, y0 + 38, text="RELATIVE BEARING / DISTANCE", fill=MUTED, font=self.small_font, anchor="nw")

        # Radar field
        self.canvas.create_oval(cx - radius, cy - radius, cx + radius, cy + radius, fill="#0D141A", outline=BORDER, width=1)
        for fraction, label in [(1.0, f"{self.radar_range_km:.0f}"), (0.66, f"{self.radar_range_km * 0.66:.0f}"), (0.33, f"{self.radar_range_km * 0.33:.0f}")]:
            r = radius * fraction
            self.canvas.create_oval(cx - r, cy - r, cx + r, cy + r, outline=GRID, width=1)
            self.canvas.create_text(cx + 8, cy - r + 10, text=f"{label} km", fill=MUTED, font=self.small_font, anchor="w")

        # Minimal crosshair
        self.canvas.create_line(cx - radius, cy, cx + radius, cy, fill=GRID_SOFT)
        self.canvas.create_line(cx, cy - radius, cx, cy + radius, fill=GRID_SOFT)

        for label, deg in (("N", 0), ("E", 90), ("S", 180), ("W", 270)):
            a = math.radians(deg)
            rr = radius + 20
            tx = cx + math.sin(a) * rr
            ty = cy - math.cos(a) * rr
            self.canvas.create_text(tx, ty, text=label, fill=MUTED, font=self.section_font)

        # Subtle sweep glow represented by two thin lines.
        sweep = math.radians(self.sweep_angle)
        for extra, color in ((3, "#294D5E"), (0, ACCENT)):
            a = sweep - math.radians(extra)
            sx = cx + math.sin(a) * radius
            sy = cy - math.cos(a) * radius
            self.canvas.create_line(cx, cy, sx, sy, fill=color, width=1 if extra else 2)

        for plane in self.visible_planes():
            distance = plane["distance"]
            bearing = plane["bearing"]
            a = math.radians(bearing)
            scale = distance / self.radar_range_km
            px = cx + math.sin(a) * radius * scale
            py = cy - math.cos(a) * radius * scale
            selected = clean_hex(plane.get("hex")) == self.selected_hex

            # Simple aircraft marker: dot + short heading line.
            track = plane.get("track")
            try:
                t = math.radians(float(track))
                hx, hy = math.sin(t), -math.cos(t)
            except (ValueError, TypeError):
                hx, hy = math.sin(a), -math.cos(a)
            size = 5 if selected else 4
            color = WARNING if selected else TEXT
            self.canvas.create_oval(px - size, py - size, px + size, py + size, fill=color, outline="")
            self.canvas.create_line(px, py, px + hx * 12, py + hy * 12, fill=color, width=2)

            callsign = clean_callsign(plane.get("flight"))
            altitude = format_altitude(plane.get("altitude_baro"))
            self.canvas.create_text(px + 9, py - 8, text=f"{callsign}  {altitude} ft", fill=color, font=self.small_font, anchor="w")

        # Station marker
        self.canvas.create_oval(cx - 5, cy - 5, cx + 5, cy + 5, fill=ACCENT, outline="")
        self.canvas.create_oval(cx - 9, cy - 9, cx + 9, cy + 9, outline=ACCENT_SOFT, width=1)
        self.canvas.create_text(cx + 12, cy + 10, text="STATION", fill=MUTED, font=self.small_font, anchor="nw")

    def draw_traffic_panel(self, x0, y0, x1, y1):
        self.rounded_rect(x0, y0, x1, y1, 18, fill=PANEL, outline=BORDER)
        self.canvas.create_text(x0 + 20, y0 + 18, text="TRAFFIC", fill=TEXT, font=self.section_font, anchor="nw")
        planes = self.visible_planes()
        self.canvas.create_text(x1 - 20, y0 + 19, text=f"{len(planes)} TRACKS", fill=MUTED, font=self.small_font, anchor="ne")

        y = y0 + 54
        row_h = 54
        for index, plane in enumerate(planes[:MAX_LIST_PLANES]):
            selected = clean_hex(plane.get("hex")) == self.selected_hex
            if selected:
                self.rounded_rect(x0 + 10, y - 4, x1 - 10, y + row_h - 5, 10, fill=PANEL_2)
                self.canvas.create_rectangle(x0 + 10, y - 4, x0 + 13, y + row_h - 5, fill=ACCENT, outline="")

            callsign = clean_callsign(plane.get("flight"))
            altitude = format_altitude(plane.get("altitude_baro"))
            speed = format_speed(plane.get("gs"))
            distance = plane.get("distance", 0.0)

            self.canvas.create_text(x0 + 22, y + 1, text=f"{index + 1:02d}", fill=MUTED, font=self.data_font, anchor="w")
            self.canvas.create_text(x0 + 48, y, text=callsign, fill=WARNING if selected else TEXT, font=self.plane_font, anchor="w")
            self.canvas.create_text(x1 - 20, y + 1, text=f"{distance:4.1f} km", fill=TEXT, font=self.data_font, anchor="e")
            self.canvas.create_text(x0 + 48, y + 22, text=f"ALT {altitude} ft     SPD {speed} kt", fill=MUTED, font=self.data_font, anchor="w")
            self.canvas.create_line(x0 + 20, y + row_h - 2, x1 - 20, y + row_h - 2, fill=BORDER)
            y += row_h

        # Selected aircraft section
        detail_y = min(y + 8, y1 - 158)
        self.canvas.create_text(x0 + 20, detail_y, text="SELECTED AIRCRAFT", fill=MUTED, font=self.small_font, anchor="nw")

        selected_plane = next((p for p in planes if clean_hex(p.get("hex")) == self.selected_hex), None)
        if selected_plane:
            self.canvas.create_text(x0 + 20, detail_y + 23, text=clean_callsign(selected_plane.get("flight")), fill=TEXT, font=self.big_font, anchor="nw")
            details = (
                f"ICAO {clean_hex(selected_plane.get('hex'))}    "
                f"HDG {format_track(selected_plane.get('track'))}\n"
                f"{selected_plane.get('lat', 0):.5f}, {selected_plane.get('lon', 0):.5f}    "
                f"{selected_plane.get('distance', 0):.1f} km"
            )
            self.canvas.create_text(x0 + 20, detail_y + 55, text=details, fill=MUTED, font=self.data_font, anchor="nw", justify="left")
        else:
            self.canvas.create_text(x0 + 20, detail_y + 27, text="Click a track on the radar", fill=MUTED, font=self.small_font, anchor="nw")

        self.canvas.create_text(x0 + 20, y1 - 26, text="D  demo/live     F  fullscreen     +/-  range     ESC  exit", fill=MUTED, font=self.small_font, anchor="sw")

    def toggle_demo(self):
        self.demo_mode = not self.demo_mode
        self.selected_hex = None

    def toggle_fullscreen(self):
        self.fullscreen = not self.fullscreen
        self.root.attributes("-fullscreen", self.fullscreen)

    def change_range(self, direction):
        current = min(range(len(RANGE_STEPS)), key=lambda i: abs(RANGE_STEPS[i] - self.radar_range_km))
        new_index = max(0, min(len(RANGE_STEPS) - 1, current + direction))
        self.radar_range_km = RANGE_STEPS[new_index]

    def mouse_click(self, event):
        planes = self.visible_planes()
        if not planes:
            return

        width = self.canvas.winfo_width()
        height = self.canvas.winfo_height()
        right_x = width * 0.70
        radar_x0, radar_y0, radar_x1, radar_y1 = 24, 92, right_x - 12, height - 18
        cx = (radar_x0 + radar_x1) / 2
        cy = (radar_y0 + radar_y1) / 2 + 4
        radius = min((radar_x1 - radar_x0) * 0.43, (radar_y1 - radar_y0) * 0.43)

        closest = None
        closest_screen = 18
        for plane in planes:
            a = math.radians(plane["bearing"])
            px = cx + math.sin(a) * radius * (plane["distance"] / self.radar_range_km)
            py = cy - math.cos(a) * radius * (plane["distance"] / self.radar_range_km)
            d = math.hypot(event.x - px, event.y - py)
            if d < closest_screen:
                closest_screen = d
                closest = plane

        # Also allow clicking an item in the traffic panel.
        if closest is None and event.x > right_x:
            row_h = 54
            start_y = 92 + 54
            idx = int((event.y - start_y + 4) // row_h)
            if 0 <= idx < min(len(planes), MAX_LIST_PLANES):
                closest = planes[idx]

        if closest:
            self.selected_hex = clean_hex(closest.get("hex"))

    def update_screen(self):
        width, height = self.canvas.winfo_width(), self.canvas.winfo_height()
        if width < 700 or height < 450:
            self.root.after(FRAME_MS, self.update_screen)
            return

        now = time.perf_counter()
        dt = min(now - self.last_update, 0.1)
        self.last_update = now

        if now - self.last_data_update >= DATA_UPDATE_MS / 1000:
            self.aircraft = get_demo_aircraft() if self.demo_mode else load_aircraft_from_readsb()
            self.last_data_update = now

        self.sweep_angle = (self.sweep_angle + self.sweep_speed * dt) % 360

        self.canvas.delete("all")
        self.draw_header(width)
        split = width * 0.70
        self.draw_radar(24, 92, split - 12, height - 18)
        self.draw_traffic_panel(split + 12, 92, width - 24, height - 18)

        self.root.after(FRAME_MS, self.update_screen)


def main():
    root = tk.Tk()
    RadarApp(root)
    root.mainloop()


if __name__ == "__main__":
    main()
