#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

// I2S-Mikrofon Konfiguration
#define I2S_WS_PIN      12       // Word Select (WS) Pin
#define I2S_SCK_PIN     13       // Serial Clock (SCK) Pin
#define I2S_SD_PIN      11       // Serial Data (SD) Pin
#define I2S_PORT        I2S_NUM_0
#define SAMPLE_RATE     16000    // Sample Rate in Hz (16kHz)
#define BUFFER_SIZE     1024     // Größe des Aufnahmepuffers
#define BIT_DEPTH       32       // INMP441 liefert 24-Bit Daten, I2S empfängt als 32-Bit

// SD-Karten Konfiguration
#define SD_CS_PIN       4        // SD Card Chip Select Pin
#define SD_MOSI_PIN     6        // SD Card MOSI
#define SD_MISO_PIN     7        // SD Card MISO
#define SD_SCK_PIN      5        // SD Card SCK
#define CONFIG_FILENAME "/config.txt"    // Name der Konfigurationsdatei
#define MAX_VALUE_LEN   64       // Maximale Länge eines Konfigurationswertes
#define MAX_FILENAME_LEN 32      // Maximale Länge des Dateinamens

// Button für Aufnahmesteuerung
#define RECORD_BUTTON_PIN 9      // Button-Pin für Aufnahmesteuerung

// WS2812 LED-Konfiguration
#define LED_PIN         48       // Pin für die WS2812-LED
#define NUM_LEDS        1        // Nur eine LED
#define LED_TYPE        WS2812   // LED-Typ
#define COLOR_ORDER     GRB      // Farbreihenfolge (meistens GRB bei WS2812)

// Status-Farben
#define COLOR_READY     CRGB(0, 64, 0)    // Grün (gedimmt) = Bereit
#define COLOR_RECORDING CRGB(255, 0, 0)   // Rot = Aufnahme
#define COLOR_ERROR     CRGB(255, 50, 0)  // Orange = Fehler
#define COLOR_UPLOAD    CRGB(0, 0, 255)   // Blau = Daten werden hochgeladen

// Task-Prioritäten
#define RECORDING_TASK_PRIORITY 10  // Hohe Priorität für Aufnahme-Task
#define UPLOAD_TASK_PRIORITY 5      // Niedrigere Priorität für Upload-Task

// Webserver Konfiguration
#define WEB_SERVER_PORT 80          // Port für den Webserver

// Queue für FTP-Upload-Tasks
QueueHandle_t uploadQueue;
SemaphoreHandle_t sdCardMutex;   // Mutex für SD-Karten-Zugriff

// Struktur für die Konfigurationsdaten
struct RecorderConfig {
    char deviceName[MAX_VALUE_LEN];     // Name des Geräts
    char wifiSSID[MAX_VALUE_LEN];       // WLAN SSID
    char wifiPassword[MAX_VALUE_LEN];   // WLAN Passwort
    char ftpServer[MAX_VALUE_LEN];      // FTP Server
    char ftpUser[MAX_VALUE_LEN];        // FTP Benutzername
    char ftpPassword[MAX_VALUE_LEN];    // FTP Passwort
    uint16_t ftpPort;                   // FTP Port
    bool ftpEnabled;                    // FTP aktiviert ja/nein
    bool webserverEnabled;              // Webserver aktiviert ja/nein
};

// WAV-Header Struktur
struct WAVHeader {
    // RIFF Header
    char riffHeader[4] = {'R', 'I', 'F', 'F'}; // RIFF Header Magic
    uint32_t wavSize = 0;                       // RIFF Chunk Size
    char waveHeader[4] = {'W', 'A', 'V', 'E'}; // WAVE Header

    // Format Header
    char fmtHeader[4] = {'f', 'm', 't', ' '};  // FMT Header
    uint32_t fmtChunkSize = 16;                // FMT Chunk Size
    uint16_t audioFormat = 1;                  // Audio Format (1 = PCM)
    uint16_t numChannels = 1;                  // Anzahl der Kanäle (1 = Mono)
    uint32_t sampleRate = SAMPLE_RATE;         // Sample Rate
    uint32_t byteRate = SAMPLE_RATE * 2;       // Byte Rate (SampleRate * NumChannels * BitsPerSample/8)
    uint16_t blockAlign = 2;                   // Block Alignment (NumChannels * BitsPerSample/8)
    uint16_t bitsPerSample = 16;               // Bits pro Sample (16 für PCM)

    // Data Header
    char dataHeader[4] = {'d', 'a', 't', 'a'}; // DATA Header
    uint32_t dataChunkSize = 0;                // Größe des Datenchunks
};

#endif // CONFIG_H