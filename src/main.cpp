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
#include "led.h"
#include "ftp.h"
#include "button.h"
#include "sdcard.h" 
#include "mic.h" 

// Globale Variablen
bool isRecording = false;        // Aufnahmestatus
CRGB leds[NUM_LEDS];             // Array für WS2812-LED


Button recordButton(RECORD_BUTTON_PIN, BUTTON_DEBOUNCE_TIME);

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
  uploadQueue = xQueueCreate(20, MAX_FILENAME_LEN * sizeof(char));
  
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

  if(config.ftpEnabled){
    // Starte Upload-Task mit niedrigerer Priorität
    xTaskCreate(
      FTPuploadTask,
      "FTP Upload Task",
      8192,
      NULL,
      UPLOAD_TASK_PRIORITY,
      NULL
    );
  }
  
  // Bereit-Signal - Pulsiere die LED grün
  for (int i = 0; i < 3; i++) {
    pulseLED(COLOR_READY);
  }
  
  // Set LED to ready status
  setLEDStatus(COLOR_READY);

  Serial.println("Recorder bereit. Drücke den Button, um die Aufnahme zu starten/stoppen.");
}

void loop() {
    
    if (recordButton.isPressed() && !isRecording) {
        startRecording();
    } else if (!recordButton.isPressed() && isRecording) {
        stopRecording();
    }
    
    // LED handling only when not recording
    if (!isRecording) {
        static uint32_t lastPulseTime = 0;
        if (millis() - lastPulseTime > 500) {
            pulseLED(COLOR_READY);
            lastPulseTime = millis();
        }
    }
    
    vTaskDelay(10);
}