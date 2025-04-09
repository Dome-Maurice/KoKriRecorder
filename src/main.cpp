#include <Arduino.h>
#include <driver/i2s.h>
#include <SD.h>
#include <FS.h>
#include <SPI.h>
#include <FastLED.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>

#include "config.h"
#include "readconfig.h"
#include "webserver.h"
 
// Globale Variablen
bool isRecording = false;        // Aufnahmestatus

CRGB leds[NUM_LEDS];             // Array für WS2812-LED

// Prototypen für in main.cpp verbleibende Funktionen
void handleButton();
void setLEDStatus(CRGB color);
void pulseLED(CRGB color);
void initLED();
void uploadTask(void* parameter);

#include "sdcard.h" 
#include "mic.h" 

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("ESP32-S3 Audio Recorder (16kHz) mit WS2812-LED");
  
  // WS2812-LED initialisieren
  initLED();
  setLEDStatus(CRGB(64, 64, 0)); // Gelb während der Initialisierung
  
  // Button mit Pull-up-Widerstand
  pinMode(RECORD_BUTTON_PIN, INPUT_PULLUP);

  // Erstelle Semaphore für SD-Karten-Zugriff
  sdCardMutex = xSemaphoreCreateMutex();
  
  // Erstelle Queue für Upload-Tasks
  uploadQueue = xQueueCreate(10, MAX_FILENAME_LEN * sizeof(char));
  
  // I2S und SD-Karte initialisieren
  bool micOk = initI2S();
  bool sdOk = initSDCard();
  delay(50);
  bool configReadOk = loadConfigFromSD();

  if (!micOk || !sdOk || !configReadOk) {
    // Mindestens eine Komponente hat Fehler
    Serial.println("Initialisierung fehlgeschlagen. System nicht bereit.");
    
    // Zeige Fehler mit speziellem Blinkmuster
    while (true) {
      // Blinke rot-orange, wenn nur das Mikrofon fehlerhaft ist
      // Blinke blau-orange, wenn nur die SD-Karte fehlerhaft ist
      // Blinke rot-blau, wenn beide fehlerhaft sind
      if (!micOk){
        setLEDStatus(CRGB(255, 0, 0));  // Rot für Mikrofon-Fehler
      }else if (!sdOk){
        setLEDStatus(CRGB(0, 0, 255));  // Blau für SD-Fehler
      }else if(!configReadOk){
        setLEDStatus(CRGB(0, 255, 255)); // Türkis für Config Lese fehler
      }
      delay(500);
      setLEDStatus(COLOR_ERROR);  // Orange als zweite Farbe
      delay(500);
    }
  }

  if(config.webserverEnabled){
    initWebServer();
  }

  // Starte Upload-Task mit niedrigerer Priorität
  xTaskCreate(
    uploadTask,
    "Upload Task",
    8192,
    NULL,
    UPLOAD_TASK_PRIORITY,
    NULL
  );
  
  // Bereit-Signal - Pulsiere die LED grün
  for (int i = 0; i < 3; i++) {
    pulseLED(COLOR_READY);
  }
  
  // Set LED to ready status
  setLEDStatus(COLOR_READY);
  Serial.println("Recorder bereit. Drücke den Button, um die Aufnahme zu starten/stoppen.");
}

void loop() {
  handleButton();
  
  // Wenn wir nicht aufnehmen, LED pulsieren lassen
  if (!isRecording) {
    static uint32_t lastPulseTime = 0;
    if (millis() - lastPulseTime > 500) {  // Alle 3 Sekunden pulsieren
      pulseLED(COLOR_READY);
      lastPulseTime = millis();
    }
  }
  
  delay(10); // Leichte Verzögerung zur CPU-Entlastung
}

void initLED() {
  //Serial.println("Initialisiere WS2812-LED...");
  
  // FastLED-Setup
  FastLED.addLeds<LED_TYPE, LED_PIN, COLOR_ORDER>(leds, NUM_LEDS)
    .setCorrection(TypicalLEDStrip)
    .setDither(false);
  
  // Setze anfängliche Helligkeit
  FastLED.setBrightness(64);  // 0-255
  
  // LED ausschalten beim Start
  fill_solid(leds, NUM_LEDS, CRGB::Black);
  FastLED.show();
  
  //Serial.println("WS2812-LED initialisiert");
}

void handleButton() {
  static uint32_t lastButtonPress = 0;
  static bool lastButtonState = HIGH;  // Pull-up, daher HIGH wenn nicht gedrückt
  
  // Debounce
  if (millis() - lastButtonPress < 300) {
    return;
  }
  
  bool buttonState = digitalRead(RECORD_BUTTON_PIN);
  
  // Erkennung von Flanken (wenn der Button gedrückt wird)
  if (buttonState == LOW && lastButtonState == HIGH) {
    lastButtonPress = millis();
    
    if (isRecording) {
      stopRecording();
    } else {
      startRecording();
    }
  }
  
  lastButtonState = buttonState;
}

void setLEDStatus(CRGB color) {
  leds[0] = color;
  FastLED.show();
}

void pulseLED(CRGB color) {
  // Sanftes Pulsieren der LED
  for (int i = 0; i < 128; i++) {
    leds[0] = color;
    leds[0].fadeToBlackBy(128 - i);
    FastLED.show();
    delay(4);
  }
  
  for (int i = 128; i >= 0; i--) {
    leds[0] = color;
    leds[0].fadeToBlackBy(128 - i);
    FastLED.show();
    delay(4);
  }
}

// Task für FTP-Upload (läuft mit niedrigerer Priorität)
void uploadTask(void* parameter) {
  char uploadFilename[MAX_FILENAME_LEN];
  
  while (true) {
    // Warte auf Dateien in der Upload-Queue
    if (xQueueReceive(uploadQueue, uploadFilename, portMAX_DELAY) == pdTRUE) {
      Serial.printf("Queuing für Upload: %s\n", uploadFilename);
      
      // Temporär LED-Status auf Upload setzen
      if (!isRecording) {  // Nur wenn keine Aufnahme läuft
        setLEDStatus(COLOR_UPLOAD);
      }
      
      // TODO: Hier kommt der FTP-Upload-Code
      // Wenn der Upload verarbeitet wird, darf er die Aufnahme nicht beeinträchtigen
      // da er mit niedrigerer Priorität läuft und durch den Mutex geschützt ist
      
      // Simuliere Upload (später durch echten FTP-Upload ersetzen)
      Serial.printf("Simuliere Upload von %s...\n", uploadFilename);
      
      // Öffne die Datei zum Lesen in kleinen Chunks, um den Speicher nicht zu überlasten
      if (xSemaphoreTake(sdCardMutex, portMAX_DELAY) == pdTRUE) {
        File fileToUpload = SD.open(uploadFilename);
        
        if (fileToUpload) {
          uint32_t fileSize = fileToUpload.size();
          Serial.printf("Datei zum Upload: %s, Größe: %u kB\n", uploadFilename, fileSize/1000);
          
          // Gebe den SD-Karten-Zugriff frei, während wir andere Dinge tun
          xSemaphoreGive(sdCardMutex);
          
          // Simuliere Upload-Zeit (etwa 1 Sekunde pro MB)
          // In der Realität würde hier der eigentliche FTP-Upload stattfinden
          uint32_t simulatedUploadTime = (fileSize / 1024) + 1000;
          Serial.printf("Simuliere Upload für %u ms...\n", simulatedUploadTime);
          
          // Verteile die Wartezeit in kleinere Stücke, um den Task nicht zu blockieren
          for (uint32_t i = 0; i < simulatedUploadTime && !isRecording; i += 100) {
            // Blinke LED während des Uploads, wenn keine Aufnahme läuft
            if (!isRecording) {
              // Pulsiere LED blau während des Uploads
              uint8_t brightness = 50 + 50 * sin(i * 0.01);
              leds[0] = COLOR_UPLOAD;
              leds[0].fadeToBlackBy(255 - brightness);
              FastLED.show();
            }
            vTaskDelay(pdMS_TO_TICKS(100));
          }
          
          // Sperren Sie die SD-Karte wieder für den Abschluss
          if (xSemaphoreTake(sdCardMutex, portMAX_DELAY) == pdTRUE) {
            fileToUpload.close();
            xSemaphoreGive(sdCardMutex);
          }
          
          Serial.printf("Upload von %s abgeschlossen.\n", uploadFilename);
        } else {
          Serial.printf("Fehler beim Öffnen der Datei für Upload: %s\n", uploadFilename);
          xSemaphoreGive(sdCardMutex);
        }
      }
      
      // LED-Status zurücksetzen, wenn keine Aufnahme läuft
      if (!isRecording) {
        setLEDStatus(COLOR_READY);
      }
    }
  }
}