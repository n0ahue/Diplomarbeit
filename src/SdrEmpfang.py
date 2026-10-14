"""
SDR-Empfang und Demodulation von Mode-S-Signalen (1090 MHz).

Ablauf:
  1. RTL-SDR liefert I/Q-Rohdaten (8 Bit) mit 2 Millionen Abtastwerten/s
  2. Betrag (Amplitude) jedes I/Q-Paares berechnen
  3. Praeambel suchen (4 Impulse in festem Muster, 8 us lang)
  4. Danach 112 Bit lesen: pro Bit 1 us = 2 Samples (Pulse-Position-Modulation)
       erste Haelfte hoeher als zweite  -> Bit = 1
       zweite Haelfte hoeher als erste  -> Bit = 0
  5. CRC pruefen - nur fehlerfreie Nachrichten werden weitergegeben

Zeitraster bei 2 MHz (1 Sample = 0,5 us):

  Sample:   0 1 2 3 4 5 6 7 8 9 10..15 | 16,17 | 18,19 | ...
  Praeambel X . X . . . . X . X  . . .  | Bit 0 | Bit 1 | ...   (X = Impuls)
"""

import numpy as np

from AdsbDecoder import crc24

SAMPLERATE = 2_000_000
FREQUENZ = 1_090_000_000
BLOCK = 256 * 1024          # Samples pro Lesevorgang

PRAEAMBEL = 16              # Samples
NACHRICHT = 224             # 112 Bit * 2 Samples
UEBERLAPP = PRAEAMBEL + NACHRICHT   # 240 Samples werden in den naechsten Block mitgenommen
DF17_BITS = np.array([1, 0, 0, 0, 1], dtype=bool)   # DF = 17 = 0b10001

# Nachschlagetabelle: (I, Q) -> Betrag. Viel schneller als sqrt() fuer jedes Sample.
_w = np.arange(256, dtype=np.float32) - 127.5
_BETRAG = np.sqrt(_w[:, None] ** 2 + _w[None, :] ** 2).astype(np.float32)


def betrag(rohdaten: bytes) -> np.ndarray:
    """Interleaved 8-Bit I/Q-Bytes -> Amplitude je Sample."""
    raw = np.frombuffer(rohdaten, dtype=np.uint8)
    return _BETRAG[raw[0::2], raw[1::2]]


def finde_nachrichten(m: np.ndarray, schwelle_faktor: float = 3.0):
    """Sucht im Amplitudenverlauf m nach gueltigen DF17-Nachrichten.
    Gibt eine Liste von 14-Byte-Nachrichten zurueck."""
    n = len(m) - UEBERLAPP
    if n <= 0:
        return []

    def s(k):                       # m[i+k] fuer alle moeglichen Startpositionen i
        return m[k:k + n]

    rauschen = float(np.median(m))
    impulse = s(0) + s(2) + s(7) + s(9)               # hier muss Energie sein
    stille = s(1) + s(3) + s(4) + s(5) + s(6) + s(8)  # hier nicht

    maske = (
        (impulse > 4 * schwelle_faktor * rauschen)    # absolut deutlich ueber Rauschen
        & (impulse > 1.4 * stille)                    # und klar staerker als die Luecken
        & (s(0) > s(1)) & (s(2) > s(1)) & (s(2) > s(3))
        & (s(7) > s(8)) & (s(9) > s(8))
    )

    ergebnis = []
    ab = 0
    for i in np.flatnonzero(maske):
        i = int(i)
        if i < ab:
            continue
        # Schnelltest: erste 5 Datenbits lesen, muessen DF17 sein
        kopf = m[i + 16:i + 26]
        if not np.array_equal(kopf[0::2] > kopf[1::2], DF17_BITS):
            continue
        # alle 112 Bit lesen
        d = m[i + 16:i + 16 + NACHRICHT]
        bits = d[0::2] > d[1::2]
        daten = np.packbits(bits).tobytes()
        if crc24(daten) == 0:
            ergebnis.append(daten)
            ab = i + UEBERLAPP          # innerhalb dieser Nachricht nicht weitersuchen
    return ergebnis


def empfange(callback, stop_event, gain="auto", ppm=0):
    """Liest dauerhaft vom RTL-SDR und ruft callback(daten) pro Nachricht auf."""
    from rtlsdr import RtlSdr           # erst hier importieren (Tests laufen ohne Stick)

    sdr = RtlSdr()
    try:
        sdr.sample_rate = SAMPLERATE
        sdr.center_freq = FREQUENZ
        sdr.gain = gain
        if ppm:
            sdr.freq_correction = ppm
        rest = np.zeros(0, dtype=np.float32)
        while not stop_event.is_set():
            m = np.concatenate((rest, betrag(sdr.read_bytes(2 * BLOCK))))
            for daten in finde_nachrichten(m):
                callback(daten)
            rest = m[-UEBERLAPP:]
    finally:
        sdr.close()