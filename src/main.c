#include <stdio.h>
#include <stdlib.h>
#include <rtl-sdr.h>

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
    
    // --- NEU: Rohdaten ausgeben ---
    printf("Die ersten 40 Rohdaten-Werte:\n");
    for (int i = 0; i < 40; i++) {
        printf("%d ", buffer[i]);
    }
    printf("\n");
    // ------------------------------
}
    // 5. Verbindung sauber trennen
    rtlsdr_close(dev);
    return 0;
}