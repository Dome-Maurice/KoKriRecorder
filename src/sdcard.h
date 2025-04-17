#ifndef SDCARD_H
#define SDCARD_H

#include <Arduino.h>
#include <SD.h>
#include <SPI.h>

SemaphoreHandle_t sdCardMutex;
uint32_t FileNumber = 0;

// Funktion, um die höchste Dateinummer auf der SD-Karte zu finden
uint32_t getHighestFileNumber() {
  uint32_t highestNumber = 0;

  if (xSemaphoreTake(sdCardMutex, portMAX_DELAY) == pdTRUE) {
      File root = SD.open("/");
      if (!root) {
          Serial.println("Fehler beim Öffnen des Root-Verzeichnisses!");
          xSemaphoreGive(sdCardMutex);
          return highestNumber;
      }

      File file = root.openNextFile();
      while (file) {
          if (!file.isDirectory()) {
              const char* name = file.name();
              uint32_t fileNumber = 0;

              // Prüfen, ob der Dateiname das Muster "xyz_XXXXXXXX.wav" hat
              if (sscanf(name, "%*[^_]_%08u.wav", &fileNumber) == 1) {
                  if (fileNumber > highestNumber) {
                      highestNumber = fileNumber;
                  }
              }
          }
          file = root.openNextFile();
      }

      root.close();
      xSemaphoreGive(sdCardMutex);
  }

  return highestNumber;
}

// SD-Karte initialisieren
bool initSDCard() {
  if (xSemaphoreTake(sdCardMutex, portMAX_DELAY) == pdTRUE) {
    // SPI Konfiguration für SD-Karte
    SPI.begin(SD_SCK_PIN, SD_MISO_PIN, SD_MOSI_PIN);
    
    if (!SD.begin(SD_CS_PIN)) {
      Serial.println("SD-Karten-Initialisierung fehlgeschlagen!");
      setLEDStatus(COLOR_ERROR);
      xSemaphoreGive(sdCardMutex);
      return false;
    }
    
    uint8_t cardType = SD.cardType();
    if (cardType == CARD_NONE) {
      Serial.println("Keine SD-Karte gefunden!");
      setLEDStatus(COLOR_ERROR);
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

  FileNumber = getHighestFileNumber();// Überprüfe, ob die SD-Karte erfolgreich initialisiert wurd

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
      KoKriRec_State = State_IDLE;
      setLEDStatus(COLOR_ERROR);
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
    // Queue leeren vor Start
    xQueueReset(audioQueue);
    
    // Sofort Mic Task starten
    TaskHandle_t micHandle;
    xTaskCreate(
        microphoneTask,
        "Microphone Task",
        8192,
        NULL,
        MIC_TASK_PRIORITY,
        &micHandle
    );

    // LED auf Aufnahme-Status setzen
    setLEDStatus(COLOR_RECORDING);
    // Kurz warten bis erste Samples da sind
    vTaskDelay(pdMS_TO_TICKS(5));
    
    recordingStartTime = millis();
    
    FileNumber++;

    // Generiere neuen Dateinamen mit fortlaufender Nummer
    sprintf(filename, "/%s_%08lu.wav", config.deviceName, FileNumber);

    if (xSemaphoreTake(sdCardMutex, portMAX_DELAY) == pdTRUE) {

        wavFile = SD.open(filename, FILE_WRITE);
        if (!wavFile) {
            Serial.println("Fehler beim Öffnen der Datei!");
            setLEDStatus(COLOR_ERROR);
            xSemaphoreGive(sdCardMutex);
            return false;
        }
        
        // WAV-Header schreiben
        writeWAVHeader();
        xSemaphoreGive(sdCardMutex);
        
        dataSize = 0;
        KoKriRec_State = State_RECORDING;
        
        
        
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

// Aufnahme beenden 
void stopRecording() {
  if (KoKriRec_State == State_RECORDING) {
    KoKriRec_State == State_IDLE;
    // Der Aufnahme-Task wird sich selbst beenden, wenn KoKriRec_State nicht State_RECORDING ist
    
    // Warte kurz, um sicherzustellen, dass der Aufnahme-Task abgeschlossen ist
    delay(100);
    
    // LED auf Bereit-Status zurücksetzen
    setLEDStatus(COLOR_IDLE);
  }
}



#endif // SDCARD_H