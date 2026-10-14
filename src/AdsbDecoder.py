"""
ADS-B Dekoder (Mode S, Downlink Format 17) - komplett selbst implementiert.

Eine ADS-B-Nachricht ist 112 Bit = 14 Byte lang:

    Bit   1-5   DF   Downlink Format (17 = ADS-B)
    Bit   6-8   CA   Faehigkeiten
    Bit   9-32  ICAO 24-Bit-Adresse des Flugzeugs
    Bit  33-88  ME   Nachrichteninhalt (56 Bit), die ersten 5 Bit = Typecode
    Bit  89-112 PI   Pruefsumme (CRC-24)

Typecodes (TC):  1-4 Kennung/Callsign | 9-18 Position+Hoehe | 19 Geschwindigkeit
"""

import math

# --------------------------------------------------------------------- CRC-24
_GENERATOR = 0xFFF409   # Mode-S-Generatorpolynom (ohne fuehrende 1)


def _crc_tabelle():
    tabelle = []
    for i in range(256):
        c = i << 16
        for _ in range(8):
            c = (c << 1) ^ _GENERATOR if c & 0x800000 else c << 1
            c &= 0xFFFFFF
        tabelle.append(c)
    return tabelle


_TABELLE = _crc_tabelle()


def crc24(daten: bytes) -> int:
    """CRC ueber alle Bytes. Bei einer fehlerfreien DF17-Nachricht (inkl. der
    24 Paritaetsbits am Ende) kommt 0 heraus."""
    crc = 0
    for b in daten:
        crc = ((crc << 8) & 0xFFFFFF) ^ _TABELLE[((crc >> 16) ^ b) & 0xFF]
    return crc


# ----------------------------------------------------------------- Dekodierung
# 6-Bit-Zeichensatz fuer das Callsign ('#' = ungueltig)
_ZEICHEN = "#ABCDEFGHIJKLMNOPQRSTUVWXYZ##### ###############0123456789######"


def _hoehe_ft(alt12):
    """12-Bit-Hoehenfeld -> Fuss. Nur das Q-Bit=1-Format (25-ft-Schritte)."""
    if not (alt12 >> 4) & 1:        # Q-Bit = 0 -> Gray-Code (alt, hier nicht unterstuetzt)
        return None
    n = ((alt12 & 0xFE0) >> 1) | (alt12 & 0xF)
    return n * 25 - 1000


def nachricht_dekodieren(daten: bytes):
    """14 Byte -> Dict mit den Informationen, oder None (ungueltig/uninteressant)."""
    if len(daten) != 14 or crc24(daten) != 0:
        return None
    if daten[0] >> 3 != 17:          # nur ADS-B (DF17)
        return None

    icao = daten[1:4].hex().upper()
    me = int.from_bytes(daten[4:11], "big")      # 56-Bit Nachrichteninhalt
    tc = me >> 51                                # Typecode

    # --- Kennung (Callsign) -------------------------------------------------
    if 1 <= tc <= 4:
        text = "".join(_ZEICHEN[(me >> (42 - 6 * i)) & 0x3F] for i in range(8))
        return {"icao": icao, "typ": "ident", "callsign": text.replace("#", "").strip()}

    # --- Position (barometrische bzw. GNSS-Hoehe) ---------------------------
    if 9 <= tc <= 18 or 20 <= tc <= 22:
        return {
            "icao": icao,
            "typ": "position",
            "hoehe_ft": _hoehe_ft((me >> 36) & 0xFFF) if tc <= 18 else None,
            "odd": bool((me >> 34) & 1),         # F-Flag: 0 = gerade, 1 = ungerade
            "lat_cpr": (me >> 17) & 0x1FFFF,
            "lon_cpr": me & 0x1FFFF,
        }

    # --- Geschwindigkeit ueber Grund (Subtype 1 und 2) ----------------------
    if tc == 19:
        subtyp = (me >> 48) & 7
        if subtyp not in (1, 2):
            return None
        faktor = 4 if subtyp == 2 else 1         # Ueberschall: 4-kt-Schritte
        v_ew = (me >> 32) & 0x3FF
        v_ns = (me >> 21) & 0x3FF
        if v_ew == 0 or v_ns == 0:
            return None
        v_ew = (v_ew - 1) * faktor * (-1 if (me >> 42) & 1 else 1)   # Ost +, West -
        v_ns = (v_ns - 1) * faktor * (-1 if (me >> 31) & 1 else 1)   # Nord +, Sued -
        speed_kt = math.hypot(v_ew, v_ns)
        kurs = math.degrees(math.atan2(v_ew, v_ns)) % 360
        vr = (me >> 10) & 0x1FF
        steigrate = None
        if vr:
            steigrate = (vr - 1) * 64 * (-1 if (me >> 19) & 1 else 1)  # ft/min
        return {"icao": icao, "typ": "velocity", "speed_kt": speed_kt,
                "kurs": kurs, "steigrate": steigrate}

    return None


# ------------------------------------------------------------------------ CPR
# Die Position wird als "Compact Position Reporting" uebertragen: je eine
# gerade und eine ungerade Nachricht werden zu Breite/Laenge kombiniert.

def _nl(lat):
    """Anzahl der Laengenzonen bei gegebener Breite (NZ = 15)."""
    lat = abs(lat)
    if lat == 0:
        return 59
    if lat == 87:
        return 2
    if lat > 87:
        return 1
    a = 1 - (1 - math.cos(math.pi / 30)) / math.cos(math.radians(lat)) ** 2
    return int(2 * math.pi / math.acos(a))


def cpr_global(lat_e, lon_e, lat_o, lon_o, neueste_ist_ungerade):
    """Globale CPR-Dekodierung aus je einer geraden und ungeraden Nachricht.
    Eingaben sind die rohen 17-Bit-Werte. Rueckgabe: (lat, lon) oder None."""
    lat_e, lon_e = lat_e / 131072, lon_e / 131072
    lat_o, lon_o = lat_o / 131072, lon_o / 131072

    j = math.floor(59 * lat_e - 60 * lat_o + 0.5)
    rlat_e = (360 / 60) * (j % 60 + lat_e)
    rlat_o = (360 / 59) * (j % 59 + lat_o)
    if rlat_e >= 270:
        rlat_e -= 360
    if rlat_o >= 270:
        rlat_o -= 360

    if _nl(rlat_e) != _nl(rlat_o):      # Flugzeug hat Laengenzone gewechselt
        return None

    if neueste_ist_ungerade:
        lat = rlat_o
        nl = _nl(lat)
        ni = max(nl - 1, 1)
        lon_cpr = lon_o
    else:
        lat = rlat_e
        nl = _nl(lat)
        ni = max(nl, 1)
        lon_cpr = lon_e

    m = math.floor(lon_e * (nl - 1) - lon_o * nl + 0.5)
    lon = (360 / ni) * (m % ni + lon_cpr)
    if lon >= 180:
        lon -= 360
    return lat, lon