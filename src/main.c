#include <stdio.h>
#include <stdlib.h>
#include <rtl-sdr.h>
#include <math.h>
#include <stdint.h>

// Mathematischer Filter zur Fehlerprüfung (CRC)
int check_crc(uint8_t *nachricht) {
    uint32_t polynom = 0xFFFA0480; // Offizielles ADS-B Generator-Polynom
    uint8_t daten[14];
    
    // Nachricht kopieren, da wir sie beim Berechnen verändern
    for (int i = 0; i < 14; i++) {
        daten[i] = nachricht[i];
    }

    // Bit für Bit durch die Nachricht gehen
    for (int i = 0; i < 112 - 24; i++) {
        int byte_index = i / 8;
        int bit_index = 7 - (i % 8);

        // Wenn das aktuelle Bit eine 1 ist, wenden wir das Polynom per XOR an
        if ((daten[byte_index] & (1 << bit_index)) != 0) {
            daten[byte_index] ^= (polynom >> 24) & 0xFF;
            daten[byte_index + 1] ^= (polynom >> 16) & 0xFF;
            daten[byte_index + 2] ^= (polynom >> 8) & 0xFF;
            daten[byte_index + 3] ^= polynom & 0xFF;
        }
    }

    // Wenn das Signal fehlerfrei ist, müssen die letzten 3 Bytes nun exakt 0 sein
    if (daten[11] == 0 && daten[12] == 0 && daten[13] == 0) {
        return 1; // Wahr (Kein Fehler, echtes Flugzeug)
    } else {
        return 0; // Falsch (Datenmüll, verwerfen)
    }
}

int main() {
    rtlsdr_dev_t *dev = NULL;
    int device_index = 0;

    // 1. SDR-Stick öffnen
    if (rtlsdr_open(&dev, device_index) < 0) {
        fprintf(stderr, "Fehler: SDR-Stick nicht gefunden oder blockiert.\n");
        return 1;
    }

    // 2. Frequenz (1090 MHz) und Sample-Rate (2 MSPS) für ADS-B einstellen
    rtlsdr_set_center_freq(dev, 1090000000); 
    rtlsdr_set_sample_rate(dev, 2000000);    
    
    // 3. Manuelle, hohe Verstärkung (Gain) setzen
    rtlsdr_set_tuner_gain_mode(dev, 1); // 1 = Manuell
    rtlsdr_set_tuner_gain(dev, 496);    // ca. 49.6 dB (sehr hohe Empfindlichkeit)

    // 4. Puffer leeren
    rtlsdr_reset_buffer(dev);
    
    uint8_t buffer[16384]; 
    printf("Hardware initialisiert. Lausche dauerhaft auf Flugzeuge (Abbruch mit Strg+C)...\n\n");
    
    // 5. Endlosschleife zum dauerhaften Empfang
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
        for (int i = 0; i < anzahl_messungen - 16; i++) {
            double spitze0 = amplituden[i];
            double spitze2 = amplituden[i+2];
            double spitze7 = amplituden[i+7];
            double spitze9 = amplituden[i+9];

            double pause1 = amplituden[i+1];
            double pause3 = amplituden[i+3];
            double pause4 = amplituden[i+4];
            double pause5 = amplituden[i+5];

            // Filter: Schwellenwert auf 1.8 gesenkt, um auch schwächere Signale zu erfassen
            if (spitze0 > 8.0 && spitze2 > 8.0 && spitze7 > 8.0 && spitze9 > 8.0) {
                double durchschnitt_spitzen = (spitze0 + spitze2 + spitze7 + spitze9) / 4.0;
                double durchschnitt_pausen = (pause1 + pause3 + pause4 + pause5) / 4.0;

                if (durchschnitt_spitzen > durchschnitt_pausen * 1.8) {
                    
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

                    // CRC-Check durchführen
                    if (!array_ende && check_crc(nachricht) == 1) {
                        int df = nachricht[0] >> 3;
                        int icao = (nachricht[1] << 16) | (nachricht[2] << 8) | nachricht[3];
                        
                        if (df == 17) {
                            printf("GÜLTIGES SIGNAL EMPFANGEN:\n");
                            printf("ICAO-Adresse: %06X\n", icao);
                            
                            int type_code = nachricht[4] >> 3;
                            printf("Type Code: %d ", type_code);

                            if (type_code >= 1 && type_code <= 4) {
                                printf("(Callsign / Flugnummer)\n");
                            } else if (type_code >= 9 && type_code <= 18) {
                                printf("(Höhe / Position)\n");
                            } else if (type_code == 19) {
                                printf("(Geschwindigkeit)\n");
                            } else {
                                printf("(Andere)\n");
                            }
                            printf("-----------------------------------\n");
                        }
                    }
                    i += 16 + (112 * 2); 
                }
            }
        }
    } 

    rtlsdr_close(dev);
    return 0;
}