#ifndef SDCARD_H
#define SDCARD_H

#include <Arduino.h>
#include <SD.h>
#include <SPI.h>
#include "config.h"
#include "recording.h"  // Neue Header-Datei für gemeinsame Funktionen

// SD-Karte initialisieren
bool initSDCard() {
  if (xSemaphoreTake(sdCardMutex, portMAX_DELAY) == pdTRUE) {
    // SPI Konfiguration für SD-Karte
    SPI.begin(SD_SCK_PIN, SD_MISO_PIN, SD_MOSI_PIN);
    
    if (!SD.begin(SD_CS_PIN)) {
      Serial.println("SD-Karten-Initialisierung fehlgeschlagen!");
      KoKriRec_State == State_RECORDING_ERROR;
      xSemaphoreGive(sdCardMutex);
      return false;
    }
    
    uint8_t cardType = SD.cardType();
    if (cardType == CARD_NONE) {
      Serial.println("Keine SD-Karte gefunden!");
      KoKriRec_State == State_RECORDING_ERROR;
      xSemaphoreGive(sdCardMutex);
      return false;
    }
    
    Serial.print("SD-Kartentyp: ");
    if (cardType == CARD_MMC) {
      Serial.println("MMC");
    } else if (cardType == CARD_SD) {
      Serial.println("SDSC");
    } else if (cardType == CARD_SDHC) {
      Serial.println("SDHC");
    } else {
      Serial.println("UNKNOWN");
    }

    Serial.println("SD-Karte initialisiert");
    uint64_t cardSize = SD.cardSize() / (1024 * 1024);
    Serial.printf("SD-Kartengröße: %lluMB\n", cardSize);
    
    xSemaphoreGive(sdCardMutex);
  }

  return true; 
}

// WAV-Header schreiben
void writeWAVHeader() {
  // WAV-Header erstellen
  WAVHeader header;
  
  // RIFF-Chunk-Größe und Daten-Chunk-Größe werden später aktualisiert
  
  // Schreibe Header in die Datei
  wavFile.write((const uint8_t *)&header, sizeof(WAVHeader));
}

// WAV-Header aktualisieren
void updateWAVHeader() {
  if (!wavFile) {
    return;
  }
  
  // Datei an den Anfang setzen
  wavFile.seek(0);
  
  // WAV-Header aktualisieren
  WAVHeader header;
  
  // RIFF-Chunk-Größe = Dateigröße - 8 Bytes für RIFF und Größe
  header.wavSize = dataSize + sizeof(WAVHeader) - 8;
  
  // Daten-Chunk-Größe = Größe der Audio-Daten
  header.dataChunkSize = dataSize;
  
  // Header in die Datei schreiben
  wavFile.write((const uint8_t *)&header, sizeof(WAVHeader));
}

// Audiodaten auf SD-Karte schreiben
bool writeAudioDataToSD(int16_t* pcmData, size_t bytesToWrite) {
  if (xSemaphoreTake(sdCardMutex, portMAX_DELAY) == pdTRUE) {
    size_t bytesWritten = wavFile.write((uint8_t*)pcmData, bytesToWrite);
    
    if (bytesWritten != bytesToWrite) {
      Serial.println("Fehler beim Schreiben auf die SD-Karte!");
      KoKriRec_State == State_RECORDING_ERROR;
      xSemaphoreGive(sdCardMutex);
      return false;
    } else {
      dataSize += bytesWritten;
    }
    
    xSemaphoreGive(sdCardMutex);
    return true;
  }
  return false;
}

// Aufnahmedatei finalisieren
void finalizeRecordingFile() {
  // Disable I2S interrupt after recording
  esp_err_t err = disable_i2s_interrupt();
  if (err != ESP_OK) {
    Serial.println("Warning: Failed to disable I2S interrupt");
  }

  if (xSemaphoreTake(sdCardMutex, portMAX_DELAY) == pdTRUE) {
    // WAV-Header aktualisieren
    updateWAVHeader();
    
    // Datei schließen
    wavFile.close();

    xSemaphoreGive(sdCardMutex);

    Serial.printf("Aufnahme beendet: %s\n", filename);
    Serial.printf("Aufnahmedauer: %lu s\n", (millis() - recordingStartTime)/1000);
    Serial.printf("Dateigröße: %lu kB\n", (dataSize + 44)/1000); // 44 Bytes für WAV-Header
    
    // Dateinamen zur Upload-Queue hinzufügen
    //char uploadFilename[MAX_FILENAME_LEN];
    //strcpy(uploadFilename, filename);
    xQueueSend(uploadQueue, filename, portMAX_DELAY);
  }
}

// Aufnahme starten und Datei öffnen
bool startRecording() {
  // Enable I2S interrupt before starting recording
  esp_err_t err = enable_i2s_interrupt();
  if (err != ESP_OK) {
    Serial.println("Failed to enable I2S interrupt");
    return false;
  }

  xQueueReset(audioQueue); 

  vTaskDelay(pdMS_TO_TICKS(10));

  recordingStartTime = millis();

  sprintf(filename, "/%s_%08lu.wav", config.deviceName, recordingStartTime/100);
  

  if (xSemaphoreTake(sdCardMutex, portMAX_DELAY) == pdTRUE) {
    // Öffne die Datei zum Schreiben
    wavFile = SD.open(filename, FILE_WRITE);
    
    if (!wavFile) {
      Serial.println("Fehler beim Öffnen der Datei!");
      KoKriRec_State = State_RECORDING_ERROR;
      xSemaphoreGive(sdCardMutex);
      return false;
    }
    
    // WAV-Header schreiben
    writeWAVHeader();
    xSemaphoreGive(sdCardMutex);
    
    dataSize = 0;
    
    setLEDStatus(COLOR_RECORDING);
    // Starte den Aufnahme-Task mit hoher Priorität
    xTaskCreate(
      recordingTask,
      "Recording Task",
      8192,
      NULL,
      RECORDING_TASK_PRIORITY,
      NULL
    );

    Serial.printf("Starte Aufnahme: %s\n", filename);
    return true;
  }
  
  return false;
}

#endif // SDCARD_H