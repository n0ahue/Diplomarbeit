import sys
import numpy as np
sys.path.insert(0, "src")
from rtlsdr import RtlSdr
from SdrEmpfang import betrag, finde_nachrichten, SAMPLERATE, FREQUENZ, BLOCK

GAIN = 40.2          # zum Testen auch 49.6 oder 30 probieren

sdr = RtlSdr()
sdr.sample_rate = SAMPLERATE
sdr.center_freq = FREQUENZ
sdr.gain = GAIN
try:
    while True:
        m = betrag(sdr.read_bytes(2 * BLOCK))
        print(f"Rauschen {np.median(m):5.1f} | Maximum {m.max():5.1f} | "
              f"Nachrichten {len(finde_nachrichten(m))}")
finally:
    sdr.close()