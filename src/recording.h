#ifndef RECORDING_H
#define RECORDING_H

#include <Arduino.h>
#include "config.h"

// Externe Variablen und Funktionen deklarieren
extern DeviceState KoKriRec_State; // Status des Geräts
extern unsigned long fileSize;          // Größe der Datei (für WAV-Header)
extern SemaphoreHandle_t sdCardMutex;
extern QueueHandle_t uploadQueue;
extern QueueHandle_t audioQueue;

// Prototypen von Funktionen, die in anderen Dateien implementiert sind
extern void setLEDStatus(CRGB color);
extern void updateWAVHeader();
extern esp_err_t readMicrophoneData(int32_t* samples, size_t* bytesRead);
extern bool writeAudioDataToSD(int16_t* pcmData, size_t bytesToWrite);
extern void finalizeRecordingFile();
extern void updateLEDFromAudio(int32_t sum, int32_t peak, int numSamples);
extern void printI2SDebugInfo();

char filename[MAX_FILENAME_LEN]; // Dateiname für die WAV-Datei
File wavFile;                    // Datei-Handle für WAV-Datei
unsigned long dataSize;          // Größe der Audio-Daten
uint32_t recordingStartTime;     // Startzeit der Aufnahme

// Audio-Aufnahme-Task-Funktion
void recordingTask(void* parameter) {
    struct AudioData audioData;
    uint32_t samplesReceived = 0;
    uint32_t lastDebugPrint = 0;

    Serial.println("Aufnahme-Task gestartet");

    while (KoKriRec_State == State_RECORDING) {
        if (xQueueReceive(audioQueue, &audioData, pdMS_TO_TICKS(100)) == pdTRUE) {
            samplesReceived++;
            
            // Debug alle 1000ms
            if (millis() - lastDebugPrint > 1000) {
                Serial.printf("Samples empfangen: %d, Bytes: %d\n", 
                            samplesReceived, audioData.bytesRead);
                lastDebugPrint = millis();
            }

            int16_t pcmData[BUFFER_SIZE];
            int32_t sum = 0;
            int32_t peak = 0;

            for (int i = 0; i < audioData.bytesRead / 4; i++) {
                int32_t sample = audioData.samples[i] >> 8;
                int32_t absSample = abs(sample);
                sum += absSample;

                if (absSample > peak) {
                    peak = absSample;
                }

                if (sample > 32767) sample = 32767;
                if (sample < -32768) sample = -32768;
                
                pcmData[i] = (int16_t)sample;
            }
            
            // Update LED visualization
            updateLEDFromAudio(sum, peak, audioData.bytesRead / 4);

            // Write audio data
            size_t bytesToWrite = audioData.bytesRead / 2;
            writeAudioDataToSD(pcmData, bytesToWrite);
        }
        
        // Debug-Info ausgeben
        printI2SDebugInfo();
    } 

    finalizeRecordingFile(); 
    vTaskDelete(NULL);
}

#endif // RECORDING_H