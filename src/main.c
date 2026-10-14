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
    rtlsdr_set_tuner_gain_mode(dev, 0); // 0 = Automatische Verstärkung (AGC)

    // 3. Puffer leeren, damit keine alten Signale gelesen werden
    rtlsdr_reset_buffer(dev);
    
    int n_read = 0;
    uint8_t buffer[16384]; // Array für die empfangenen Bytes
    
    printf("Hardware initialisiert. Lese erste Rohdaten...\n");
    
    // 4. Synchrones Lesen eines Datenblocks
    if (rtlsdr_read_sync(dev, buffer, sizeof(buffer), &n_read) < 0) {
        fprintf(stderr, "Fehler beim Lesen der Funkdaten.\n");
    } else {
        printf("%d Bytes erfolgreich gelesen!\n", n_read);

        // 1. Alle rohen Bytes in echte Amplituden umwandeln
        int anzahl_messungen = n_read / 2;
        double amplituden[8192];

        for (int i = 0; i < n_read; i += 2) {
            double i_wert = (double)buffer[i] - 127.0;
            double q_wert = (double)buffer[i+1] - 127.0;
            amplituden[i/2] = sqrt((i_wert * i_wert) + (q_wert * q_wert));
        }

        // 2. Nach der Präambel suchen
        int treffer = 0;
        for (int i = 0; i < anzahl_messungen - 16; i++) {
            // Die 4 erwarteten Signalspitzen
            double spitze0 = amplituden[i];
            double spitze2 = amplituden[i+2];
            double spitze7 = amplituden[i+7];
            double spitze9 = amplituden[i+9];

            // Einige der erwarteten Pausen
            double pause1 = amplituden[i+1];
            double pause3 = amplituden[i+3];
            double pause4 = amplituden[i+4];
            double pause5 = amplituden[i+5];

            // Ein einfacher Filter: Die Spitzen müssen stärker als 10.0 sein 
            // und deutlich größer als die direkten Pausen daneben.
            if (spitze0 > 10.0 && spitze2 > 10.0 && spitze7 > 10.0 && spitze9 > 10.0) {
                
                double durchschnitt_spitzen = (spitze0 + spitze2 + spitze7 + spitze9) / 4.0;
                double durchschnitt_pausen = (pause1 + pause3 + pause4 + pause5) / 4.0;

                // Ist das Signal mindestens dreimal so stark wie das Rauschen dazwischen?
                if (durchschnitt_spitzen > durchschnitt_pausen * 3.0) {
                    
                    uint8_t nachricht[14] = {0}; // 112 Bits = 14 Bytes
                    int array_ende = 0;

                    // Wir lesen 112 Bits aus
                    for (int b = 0; b < 112; b++) {
                        // Startindex des Bits berechnen (16 Messungen für Präambel + 2 Messungen pro Bit)
                        int bit_index = i + 16 + (b * 2);
                        
                        // Sicherheitscheck: Sind wir am Ende der eingelesenen Daten?
                        if (bit_index + 1 >= anzahl_messungen) {
                            array_ende = 1;
                            break;
                        }

                        // Die zwei Hälften des Bits vergleichen
                        int bit_wert = 0;
                        if (amplituden[bit_index] > amplituden[bit_index + 1]) {
                            bit_wert = 1;
                        } else {
                            bit_wert = 0;
                        }

                        // Das Bit an die richtige Stelle im Byte schieben
                        int byte_index = b / 8;
                        nachricht[byte_index] = (nachricht[byte_index] << 1) | bit_wert;
                    }

                    if (!array_ende) {
                        // Fehlerprüfung durchführen
                        if (check_crc(nachricht) == 1) {
                            
                            int df = nachricht[0] >> 3;
                            int icao = (nachricht[1] << 16) | (nachricht[2] << 8) | nachricht[3];
                            
                            printf("GÜLTIGES SIGNAL EMPFANGEN:\n");
                            printf("Rohdaten (Hex): ");
                            for (int j = 0; j < 14; j++) {
                                printf("%02X", nachricht[j]);
                            }
                            printf("\n");
                            printf("Downlink Format: %d\n", df);
                            printf("ICAO-Adresse: %06X\n", icao);

                            if (df == 17) {
                                printf("--> Das ist eine DF17 Nachricht mit Positions-/Höhendaten!\n");
                            }
                            printf("-----------------------------------\n\n");
                            treffer++;
                        }
                    }
                    
                    // Wir überspringen das gelesene Datenpaket
                    i += 16 + (112 * 2); 
                }
            }
        }
        printf("Insgesamt %d GÜLTIGE Flugzeug-Nachrichten in diesem Block gefunden.\n", treffer);
    } 

    // 5. Verbindung sauber trennen
    rtlsdr_close(dev);
    return 0;
}