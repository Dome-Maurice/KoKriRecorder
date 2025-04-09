#ifndef RECORDING_H
#define RECORDING_H

#include <Arduino.h>
#include "config.h"

// Externe Variablen und Funktionen deklarieren
extern bool isRecording;
extern unsigned long fileSize;          // Größe der Datei (für WAV-Header)
extern SemaphoreHandle_t sdCardMutex;
extern QueueHandle_t uploadQueue;

// Prototypen von Funktionen, die in anderen Dateien implementiert sind
extern void setLEDStatus(CRGB color);
extern void updateWAVHeader();
extern esp_err_t readMicrophoneData(int32_t* samples, size_t* bytesRead);
extern bool writeAudioDataToSD(int16_t* pcmData, size_t bytesToWrite);
extern void finalizeRecordingFile();

char filename[MAX_FILENAME_LEN]; // Dateiname für die WAV-Datei
File wavFile;                    // Datei-Handle für WAV-Datei
unsigned long dataSize;          // Größe der Audio-Daten
uint32_t recordingStartTime;     // Startzeit der Aufnahme

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
    esp_err_t result = readMicrophoneData(samples, &bytesRead);
    
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

      // Schreibe konvertierte Daten auf SD-Karte
      size_t bytesToWrite = bytesRead / 2; // 16-bit anstatt 32-bit
      writeAudioDataToSD(pcmData, bytesToWrite);
    }
    
    // Kurze Verzögerung, um anderen Tasks CPU-Zeit zu geben
    vTaskDelay(1);
  }
  
  // Aufnahme abgeschlossen, WAV-Header aktualisieren und Datei schließen
  finalizeRecordingFile();
  
  // Zeige Aufnahme-Beendigung mit Blinken an
  FastLED.setBrightness(64);
  for (int i = 0; i < 3; i++) {
    setLEDStatus(CRGB::Black);
    delay(100);
    setLEDStatus(COLOR_READY);
    delay(100);
  }
  
  // Task beenden
  vTaskDelete(NULL);
}

#endif // RECORDING_H