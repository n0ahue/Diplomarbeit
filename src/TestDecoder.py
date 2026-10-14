"""
Tests ohne Hardware:  python3 tests/test_dekoder.py

1. Dekoder gegen bekannte Beispielnachrichten (aus dem Buch "The 1090 MHz Riddle")
2. Demodulator: Nachrichten werden als simuliertes I/Q-Signal mit Rauschen erzeugt
   und muessen wieder gefunden werden.
"""
import os
import sys

import numpy as np

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "src"))
from AdsbDecoder import cpr_global, nachricht_dekodieren
from SdrEmpfang import betrag, finde_nachrichten

IDENT = "8D4840D6202CC371C32CE0576098"
POS_GERADE = "8D40621D58C382D690C8AC2863A7"
POS_UNGERADE = "8D40621D58C386435CC412692AD6"
VELOCITY = "8D485020994409940838175B284F"


def test_dekoder():
    m = nachricht_dekodieren(bytes.fromhex(IDENT))
    assert m["icao"] == "4840D6" and m["callsign"] == "KLM1023", m

    e = nachricht_dekodieren(bytes.fromhex(POS_GERADE))
    o = nachricht_dekodieren(bytes.fromhex(POS_UNGERADE))
    assert e["hoehe_ft"] == 38000 and not e["odd"] and o["odd"]
    lat, lon = cpr_global(e["lat_cpr"], e["lon_cpr"], o["lat_cpr"], o["lon_cpr"], True)
    assert abs(lat - 52.2657) < 0.001 and abs(lon - 3.9389) < 0.001, (lat, lon)

    v = nachricht_dekodieren(bytes.fromhex(VELOCITY))
    assert round(v["speed_kt"]) == 159 and abs(v["kurs"] - 182.88) < 0.01 and v["steigrate"] == -832, v

    kaputt = bytearray.fromhex(IDENT)
    kaputt[5] ^= 0x01                      # ein Bit verfaelschen -> CRC muss fehlschlagen
    assert nachricht_dekodieren(bytes(kaputt)) is None
    print("Dekoder-Test OK")


def signal_erzeugen(nachrichten, rng, amplitude=80, rauschen=4):
    """Simuliertes 8-Bit-I/Q-Signal (2 MHz) mit den Nachrichten dazwischen."""
    mag = np.zeros(0)
    for hexstr in nachrichten:
        bits = np.unpackbits(np.frombuffer(bytes.fromhex(hexstr), dtype=np.uint8))
        s = np.zeros(240)
        s[[0, 2, 7, 9]] = 1                       # Praeambel
        s[16::2] = bits                           # 1 = Impuls in erster Haelfte
        s[17::2] = 1 - bits                       # 0 = Impuls in zweiter Haelfte
        mag = np.concatenate((mag, np.zeros(int(rng.integers(500, 3000))), s * amplitude))
    mag = np.concatenate((mag, np.zeros(2000)))
    phase = rng.uniform(0, 2 * np.pi, len(mag))
    i = 127.5 + mag * np.cos(phase) + rng.normal(0, rauschen, len(mag))
    q = 127.5 + mag * np.sin(phase) + rng.normal(0, rauschen, len(mag))
    roh = np.empty(2 * len(mag), dtype=np.uint8)
    roh[0::2] = np.clip(i, 0, 255)
    roh[1::2] = np.clip(q, 0, 255)
    return roh.tobytes()


def test_demodulator():
    rng = np.random.default_rng(1)
    gesendet = [IDENT, POS_GERADE, POS_UNGERADE, VELOCITY] * 5
    gefunden = [d.hex().upper() for d in finde_nachrichten(betrag(signal_erzeugen(gesendet, rng)))]
    assert gefunden == gesendet, f"{len(gefunden)} von {len(gesendet)} gefunden"
    # reines Rauschen darf nichts liefern
    rauschen = rng.normal(127.5, 4, 400000).clip(0, 255).astype(np.uint8).tobytes()
    assert finde_nachrichten(betrag(rauschen)) == []
    print(f"Demodulator-Test OK ({len(gefunden)} von {len(gesendet)} Nachrichten gefunden)")


if __name__ == "__main__":
    test_dekoder()
    test_demodulator()