#!/usr/bin/env python3
"""
Flugzeug-Tracker: RTL-SDR -> eigener ADS-B-Dekoder -> Tabelle im Terminal.

Start:    python3 src/main.py
Beenden:  Strg+C
"""

import math
import threading
import time

from rich.console import Console
from rich.live import Live
from rich.table import Table

from AdsbDecoder import cpr_global, nachricht_dekodieren
from SdrEmpfang import empfange

# ---------------------------------------------------------------- Einstellungen
HOME_LAT = 47.8095      # <- Standort der Antenne eintragen
HOME_LON = 13.0550
MAX_KM = 600            # Positionen weiter weg gelten als Fehldekodierung
TIMEOUT_S = 60          # Flugzeug entfernen, wenn so lange nichts mehr kam
CPR_PAAR_S = 10         # gerade+ungerade Position muessen so nah beieinander liegen
GAIN = "auto"           # oder Zahl in dB, z.B. 40.2 (Verstaerkung des RTL-SDR)
# ------------------------------------------------------------------------------


def distanz_km(lat1, lon1, lat2, lon2):
    """Luftlinie (Haversine-Formel)."""
    p1, p2 = math.radians(lat1), math.radians(lat2)
    a = (math.sin((p2 - p1) / 2) ** 2
         + math.cos(p1) * math.cos(p2) * math.sin(math.radians(lon2 - lon1) / 2) ** 2)
    return 2 * 6371.0 * math.asin(math.sqrt(a))


class Tracker:
    """Sammelt die dekodierten Nachrichten pro Flugzeug (ICAO-Adresse)."""

    def __init__(self):
        self.flugzeuge = {}
        self.lock = threading.Lock()
        self.nachrichten = 0

    def verarbeite(self, daten):
        msg = nachricht_dekodieren(daten)
        if msg is None:
            return
        jetzt = time.time()
        with self.lock:
            self.nachrichten += 1
            ac = self.flugzeuge.setdefault(msg["icao"], {"hex": msg["icao"], "msgs": 0, "cpr": {}})
            ac["msgs"] += 1
            ac["zuletzt"] = jetzt

            if msg["typ"] == "ident":
                ac["callsign"] = msg["callsign"]

            elif msg["typ"] == "velocity":
                ac["speed_kt"] = msg["speed_kt"]
                ac["kurs"] = msg["kurs"]
                if msg["steigrate"] is not None:
                    ac["steigrate"] = msg["steigrate"]

            elif msg["typ"] == "position":
                if msg["hoehe_ft"] is not None:
                    ac["hoehe_ft"] = msg["hoehe_ft"]
                ac["cpr"][msg["odd"]] = (msg["lat_cpr"], msg["lon_cpr"], jetzt)
                anderes = ac["cpr"].get(not msg["odd"])
                if anderes and jetzt - anderes[2] <= CPR_PAAR_S:
                    gerade = anderes if msg["odd"] else ac["cpr"][False]
                    ungerade = ac["cpr"][True]
                    pos = cpr_global(gerade[0], gerade[1], ungerade[0], ungerade[1], msg["odd"])
                    if pos and distanz_km(HOME_LAT, HOME_LON, *pos) <= MAX_KM:
                        ac["lat"], ac["lon"] = pos

    def tabelle(self, fehler=None):
        jetzt = time.time()
        with self.lock:
            for h in [h for h, a in self.flugzeuge.items() if jetzt - a["zuletzt"] > TIMEOUT_S]:
                del self.flugzeuge[h]
            kopie = [dict(a) for a in self.flugzeuge.values()]
            gesamt = self.nachrichten

        t = Table(title=f"Flugzeuge in Reichweite  ({time.strftime('%H:%M:%S')})")
        for name, rechts in [("Hex", 0), ("Flug", 0), ("Hoehe [ft]", 1), ("Speed [km/h]", 1),
                             ("Kurs [°]", 1), ("Steigrate [ft/min]", 1), ("Lat", 1), ("Lon", 1),
                             ("Distanz [km]", 1), ("Msgs", 1), ("Alter [s]", 1)]:
            t.add_column(name, justify="right" if rechts else "left")

        def f(wert, nk=0):
            return "-" if wert is None else f"{wert:.{nk}f}"

        zeilen = []
        for a in kopie:
            dist = None
            if "lat" in a:
                dist = distanz_km(HOME_LAT, HOME_LON, a["lat"], a["lon"])
            zeilen.append((dist if dist is not None else 1e9, dist, a))

        for _, dist, a in sorted(zeilen, key=lambda z: z[0]):
            kmh = a["speed_kt"] * 1.852 if "speed_kt" in a else None
            t.add_row(a["hex"], a.get("callsign", "-"), f(a.get("hoehe_ft")), f(kmh),
                      f(a.get("kurs")), f(a.get("steigrate")), f(a.get("lat"), 4),
                      f(a.get("lon"), 4), f(dist, 1), str(a["msgs"]), f"{jetzt - a['zuletzt']:.0f}")

        t.caption = f"{len(kopie)} Flugzeug(e) | {gesamt} gueltige Nachrichten (CRC ok)"
        if fehler:
            t.caption += f" | FEHLER: {fehler[0]}"
        return t


def main():
    tracker = Tracker()
    stop = threading.Event()
    fehler = []

    def sdr_thread():
        try:
            empfange(tracker.verarbeite, stop, gain=GAIN)
        except Exception as e:                  # z.B. Stick nicht gefunden
            fehler.append(f"{type(e).__name__}: {e}")

    threading.Thread(target=sdr_thread, daemon=True).start()
    try:
        with Live(tracker.tabelle(), console=Console(), refresh_per_second=2) as live:
            while True:
                live.update(tracker.tabelle(fehler))
                time.sleep(1)
    except KeyboardInterrupt:
        stop.set()


if __name__ == "__main__":
    main()