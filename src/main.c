#include <stdio.h>
#include <stdlib.h>
#include <rtl-sdr.h>
#include <math.h>
#include <stdint.h>

// Mathematische CRC-Prüfung (ADS-B Generator-Polynom 0xFFFA0480)
int check_crc(uint8_t *nachricht) {
    uint32_t polynom = 0xFFFA0480;
    uint8_t daten[14];
    
    for (int i = 0; i < 14; i++) { 
        daten[i] = nachricht[i]; 
    }

    for (int i = 0; i < 112 - 24; i++) {
        int byte_index = i / 8;
        int bit_index = 7 - (i % 8);

        if ((daten[byte_index] & (1 << bit_index)) != 0) {
            daten[byte_index] ^= (polynom >> 24) & 0xFF;
            daten[byte_index + 1] ^= (polynom >> 16) & 0xFF;
            daten[byte_index + 2] ^= (polynom >> 8) & 0xFF;
            daten[byte_index + 3] ^= polynom & 0xFF;
        }
    }

    return (daten[11] == 0 && daten[12] == 0 && daten[13] == 0) ? 1 : 0;
}

// CRC mit 1-Bit-Fehlerkorrektur
int check_and_fix_crc(uint8_t *nachricht) {
    // 1. Unveränderte Nachricht testen
    if (check_crc(nachricht) == 1) {
        return 1; // 100% fehlerfrei empfangen
    }

    // 2. Versuch: Jedes der 112 Bits einzeln kippen und testen
    for (int b = 0; b < 112; b++) {
        int byte_idx = b / 8;
        int bit_mask = 1 << (7 - (b % 8));

        nachricht[byte_idx] ^= bit_mask; // Bit testweise drehen

        if (check_crc(nachricht) == 1) {
            return 2; // Erreicht: 1-Bit-Fehler erfolgreich repariert!
        }

        nachricht[byte_idx] ^= bit_mask; // Bit wieder zurückdrehen
    }

    return 0; // Zu stark gestört / Datenmüll
}

int main() {
    rtlsdr_dev_t *dev = NULL;
    int device_index = 0;

    if (rtlsdr_open(&dev, device_index) < 0) {
        fprintf(stderr, "Fehler: SDR-Stick nicht gefunden oder blockiert.\n");
        return 1;
    }

    // Frequenz 1090 MHz, Sample-Rate 2 MSPS
    rtlsdr_set_center_freq(dev, 1090000000); 
    rtlsdr_set_sample_rate(dev, 2000000);    
    
    // Maximale Verstärkung für hohe Empfindlichkeit
    rtlsdr_set_tuner_gain_mode(dev, 1);
    rtlsdr_set_tuner_gain(dev, 496); // 49.6 dB

    rtlsdr_reset_buffer(dev);
    
    uint8_t buffer[16384]; 
    printf("Hardware initialisiert. Lausche mit Debug-Modus auf Flugzeuge (Abbruch mit Strg+C)...\n\n");
    
    while (1) {
        int n_read = 0;
        if (rtlsdr_read_sync(dev, buffer, sizeof(buffer), &n_read) < 0) {
            fprintf(stderr, "Fehler beim Lesen der Funkdaten.\n");
            break;
        } 

        int anzahl_messungen = n_read / 2;
        double amplituden[8192];

        // Rohe I/Q-Bytes in Amplituden umwandeln
        for (int i = 0; i < n_read; i += 2) {
            double i_wert = (double)buffer[i] - 127.0;
            double q_wert = (double)buffer[i+1] - 127.0;
            amplituden[i/2] = sqrt((i_wert * i_wert) + (q_wert * q_wert));
        }

        // Nach Präambeln suchen
        for (int i = 0; i < anzahl_messungen - 240; i++) {
            // Durchschnitt der 4 Präambel-Spitzen
            double spitzen = (amplituden[i] + amplituden[i+2] + amplituden[i+7] + amplituden[i+9]) / 4.0;
            
            // Durchschnitt der Pausen dazwischen
            double pausen = (amplituden[i+1] + amplituden[i+3] + amplituden[i+4] + 
                             amplituden[i+5] + amplituden[i+6] + amplituden[i+8]) / 6.0;

            // Dynamische Erkennung
            if (spitzen > 4.5 && spitzen > pausen * 1.25) {
                
                uint8_t nachricht[14] = {0}; 
                int array_ende = 0;

                // 112 Bits demodulieren (PPM)
                for (int b = 0; b < 112; b++) {
                    int bit_index = i + 16 + (b * 2);
                    if (bit_index + 1 >= anzahl_messungen) {
                        array_ende = 1;
                        break;
                    }
                    int bit_wert = (amplituden[bit_index] > amplituden[bit_index + 1]) ? 1 : 0;
                    int byte_index = b / 8;
                    nachricht[byte_index] = (nachricht[byte_index] << 1) | bit_wert;
                }

                if (!array_ende) {
                    int df = nachricht[0] >> 3;
                    int icao = (nachricht[1] << 16) | (nachricht[2] << 8) | nachricht[3];

                    // Wir filtern auf DF17 (ADS-B Extended Squitter)
                    if (df == 17) {
                        int crc_status = check_and_fix_crc(nachricht);

                        if (crc_status > 0) {
                            int type_code = nachricht[4] >> 3;
                            printf("[GÜLTIG - CRC OK] ICAO: %06X | Type Code: %d ", icao, type_code);
                            if (crc_status == 2) {
                                printf("(1-Bit repariert)");
                            }
                            printf("\n-----------------------------------\n");
                        } else {
                            // Signal erfasst, aber CRC schlug fehl
                            printf("[Signal erfasst - CRC fehlgeschlagen] Raw ICAO: %06X\n", icao);
                        }

                        // Bei verarbeitetem Signal überspringen
                        i += 16 + (112 * 2) - 1; 
                    }
                }
            }
        }
    } 

    rtlsdr_close(dev);
    return 0;
}