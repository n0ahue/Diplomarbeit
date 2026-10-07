#include <stdio.h>
#include <stdlib.h>
#include <rtl-sdr.h>
#include <math.h>

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
            printf("Mögliche Präambel gefunden bei Index %d! (Signalstärke: %.1f)\n", i, durchschnitt_spitzen);
            treffer++;
            
            // Wir überspringen das restliche Datenpaket, um Doppelzählungen zu vermeiden
            i += 112 * 2; 
        }
    }
}

printf("Insgesamt %d Flugzeug-Präambeln in diesem Block gefunden.\n", treffer);
    // 5. Verbindung sauber trennen
    rtlsdr_close(dev);
    return 0;
}
}