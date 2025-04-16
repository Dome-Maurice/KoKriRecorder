#ifndef AUDIO_MANAGER_H
#define AUDIO_MANAGER_H

#include <Arduino.h>
#include <driver/i2s.h>
#include "config.h"
#include <SD.h>

// Struktur für Audio-Daten
struct AudioData {
    int32_t samples[BUFFER_SIZE];
    size_t bytesRead;
};

// Externe Variablen
extern DeviceState KoKriRec_State;
extern unsigned long fileSize;
extern SemaphoreHandle_t sdCardMutex;
extern QueueHandle_t uploadQueue;

// Globale Variablen
extern char filename[MAX_FILENAME_LEN];
extern File wavFile;
extern unsigned long dataSize;
extern uint32_t recordingStartTime;
extern QueueHandle_t audioQueue;

// Funktionsdeklarationen
bool initI2S();
esp_err_t readMicrophoneData(int32_t* samples, size_t* bytesRead);
void recordingTask(void* parameter);
void microphoneTask(void* parameter);

// Externe Funktionen
extern void updateWAVHeader();
extern bool writeAudioDataToSD(int16_t* pcmData, size_t bytesToWrite);
extern void finalizeRecordingFile();
extern void updateLEDFromAudio(int32_t sum, int32_t peak, int numSamples);

#endif // AUDIO_MANAGER_H