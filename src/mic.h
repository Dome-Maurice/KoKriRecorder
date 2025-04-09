#ifndef MIC_H
#define MIC_H

#include <Arduino.h>
#include <driver/i2s.h>
#include "config.h"

// I2S-Mikrofon initialisieren
bool initI2S() {
  // I2S-Konfiguration
  i2s_config_t i2s_config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
    .sample_rate = SAMPLE_RATE,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 8,
    .dma_buf_len = BUFFER_SIZE,
    .use_apll = false,
    .tx_desc_auto_clear = false,
    .fixed_mclk = 0
  };
  
  // I2S-Pin-Konfiguration
  i2s_pin_config_t pin_config = {
    .bck_io_num = I2S_SCK_PIN,
    .ws_io_num = I2S_WS_PIN,
    .data_out_num = I2S_PIN_NO_CHANGE,
    .data_in_num = I2S_SD_PIN
  };
  
  // I2S-Treiber installieren
  esp_err_t err = i2s_driver_install(I2S_PORT, &i2s_config, 0, NULL);
  if (err != ESP_OK) {
    Serial.printf("Fehler bei der I2S-Treiberinstallation: %d\n", err);
    
    // Detaillierteres Error-Handling
    switch(err) {
      case ESP_ERR_INVALID_ARG:
        Serial.println("Ungültige Argumente für i2s_driver_install");
        break;
      case ESP_ERR_NO_MEM:
        Serial.println("Nicht genügend Speicher für I2S-Treiber");
        break;
      case ESP_ERR_INVALID_STATE:
        Serial.println("I2S-Treiber bereits installiert");
        // Versuche, den Treiber zu deinstallieren und erneut zu installieren
        i2s_driver_uninstall(I2S_PORT);
        err = i2s_driver_install(I2S_PORT, &i2s_config, 0, NULL);
        if (err != ESP_OK) {
          Serial.println("Erneute Installation fehlgeschlagen");
          setLEDStatus(COLOR_ERROR);
          return false;
        }
        Serial.println("I2S-Treiber neu installiert");
        break;
      default:
        Serial.println("Unbekannter I2S-Treiberfehler");
        break;
    }
    
    if (err != ESP_OK) {
      setLEDStatus(COLOR_ERROR);
      return false;
    }
  }
  
  // I2S-Pins konfigurieren
  err = i2s_set_pin(I2S_PORT, &pin_config);
  if (err != ESP_OK) {
    Serial.printf("Fehler bei der I2S-Pin-Konfiguration: %d\n", err);
    
    // Detaillierteres Error-Handling für Pin-Konfiguration
    switch(err) {
      case ESP_ERR_INVALID_ARG:
        Serial.println("Ungültige Pin-Konfiguration");
        // Prüfe, ob Pins bereits für andere Funktionen verwendet werden
        Serial.printf("Prüfe Pins: SCK=%d, WS=%d, SD=%d\n", I2S_SCK_PIN, I2S_WS_PIN, I2S_SD_PIN);
        break;
      default:
        Serial.println("Unbekannter Pin-Konfigurationsfehler");
        break;
    }
    
    // Bereinige bei Fehler
    i2s_driver_uninstall(I2S_PORT);
    setLEDStatus(COLOR_ERROR);
    return false;
  }
  
  Serial.println("I2S-Mikrofon initialisiert");
  return true;
}

// Audio-Aufnahme-Task-Funktion
void recordingTask(void* parameter) {
  Serial.println("Aufnahme-Task gestartet");
  
  // Variablen für Nachhalleffekt
  uint8_t currentBrightness = 64;
  uint8_t targetBrightness = 64;
  float decayFactor = 0.8;  // Nachklangfaktor: Höher = längerer Nachklang

  while (isRecording) {
    // Audio aufnehmen und auf SD-Karte schreiben
    int32_t samples[BUFFER_SIZE];
    size_t bytesRead = 0;
    
    // Daten vom Mikrofon lesen
    esp_err_t result = i2s_read(I2S_PORT, &samples, sizeof(samples), &bytesRead, portMAX_DELAY);
    
    if (result == ESP_OK && bytesRead > 0 && isRecording) {
      // Konvertiere 32-bit INMP441 Daten zu 16-bit PCM für WAV
      int16_t pcmData[BUFFER_SIZE];

      // Audio-Pegel für LED-Steuerung berechnen
      int32_t sum = 0;
      int32_t peak = 0;

      // Konvertierung und Normalisierung der Samples
      for (int i = 0; i < bytesRead / 4; i++) {
        // INMP441 liefert Daten im Bereich links ausgerichtet (MSB),
        // wir müssen sie um 8 Bits nach rechts verschieben und auf 16 Bit reduzieren
        int32_t sample = samples[i] >> 8;
        
        // Audio-Pegel berechnen (absoluter Wert des Samples)
        int32_t absSample = abs(sample);
        sum += absSample;

        // Peak aktualisieren
        if (absSample > peak) {
          peak = absSample;
        }

        // Begrenze auf 16-bit Signed Integer Bereich
        if (sample > 32767) sample = 32767;
        if (sample < -32768) sample = -32768;
        
        pcmData[i] = (int16_t)sample;
      }
      
      // LED-Helligkeit basierend auf Audio-Pegel anpassen
      int numSamples = bytesRead / 4;
      if (numSamples > 0) {
        // Berechne Durchschnitt und skaliere ihn
        int32_t average = sum / numSamples;
        
        // Kombiniere Durchschnitt und Peak für dynamischeres Verhalten
        // Peak stärker gewichten für bessere visuelle Reaktion
        int32_t combinedLevel = average * 0.3 + peak * 0.9;
        
        // Skaliere auf 64-255 für Helligkeit, mit stärkerer Skalierung
        targetBrightness = constrain(64 + (combinedLevel / 80), 64, 255);
        
        // Nachhall-Effekt: Langsam zum Zielwert bewegen
        if (targetBrightness > currentBrightness) {
          // Schnell heller werden (bei lautem Ton)
          currentBrightness = targetBrightness;
        } else {
          // Langsam dunkler werden (Nachhall)
          currentBrightness = currentBrightness * decayFactor + targetBrightness * (1 - decayFactor);
        }
        
        // LED auf Rot setzen mit berechneter Helligkeit
        leds[0] = COLOR_RECORDING;
        
        // Für deutlichere Reaktion bei hohen Pegeln: Färbung leicht ändern
        if (currentBrightness > 180) {
          // Bei hoher Lautstärke etwas mehr ins Orange gehen
          leds[0].g = map(currentBrightness, 180, 255, 0, 70);
        }
        
        FastLED.setBrightness(currentBrightness);
        FastLED.show();
      }

      if (xSemaphoreTake(sdCardMutex, portMAX_DELAY) == pdTRUE) {
        // Schreibe konvertierte Daten auf SD-Karte
        size_t bytesToWrite = bytesRead / 2; // 16-bit anstatt 32-bit
        size_t bytesWritten = wavFile.write((uint8_t*)pcmData, bytesToWrite);
        
        if (bytesWritten != bytesToWrite) {
          Serial.println("Fehler beim Schreiben auf die SD-Karte!");
          isRecording = false;
          setLEDStatus(COLOR_ERROR);
        } else {
          dataSize += bytesWritten;
        }
        
        xSemaphoreGive(sdCardMutex);
      }
      
    }
    
    // Kurze Verzögerung, um anderen Tasks CPU-Zeit zu geben
    vTaskDelay(1);
  }
  
  // Aufnahme abgeschlossen, WAV-Header aktualisieren und Datei schließen
  unsigned long recordingDuration = millis() - recordingStartTime;

  if (xSemaphoreTake(sdCardMutex, portMAX_DELAY) == pdTRUE) {
    // WAV-Header aktualisieren
    updateWAVHeader();
    
    // Datei schließen
    wavFile.close();
    
    // Generiere einen eindeutigen Dateinamen basierend auf der aktuellen Zeit
    sprintf(filename, "/%s_%08lu.wav", config.deviceName, recordingStartTime);

    xSemaphoreGive(sdCardMutex);

    Serial.printf("Aufnahme beendet: %s\n", filename);
    Serial.printf("Aufnahmedauer: %lu s\n", recordingDuration/1000);
    Serial.printf("Dateigröße: %lu kB\n", (dataSize + 44)/1000); // 44 Bytes für WAV-Header
    
    // Dateinamen zur Upload-Queue hinzufügen
    char uploadFilename[MAX_FILENAME_LEN];
    strcpy(uploadFilename, filename);
    xQueueSend(uploadQueue, uploadFilename, portMAX_DELAY);
    
    // Setze LED-Helligkeit zurück
    FastLED.setBrightness(64);
    
    // Zeige Aufnahme-Beendigung mit Blinken an
    for (int i = 0; i < 3; i++) {
      setLEDStatus(CRGB::Black);
      delay(100);
      setLEDStatus(COLOR_READY);
      delay(100);
    }
  }
  
  // Task beenden
  vTaskDelete(NULL);
}

#endif // MIC_H