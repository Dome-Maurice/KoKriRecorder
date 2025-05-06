#include <Arduino.h>
#include <driver/i2s.h>
#include <SD.h>
#include <FS.h>
#include <SPI.h>
#include <FastLED.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>

#include "config.h"
#include "audio_manager.h"
#include "led.h"
#include "readconfig.h"
#include "webserver.h"
#include "ftp.h"
#include "button.h"
#include "sdcard.h" 

CRGB statusled[1];             // Onboard LED
CRGB effektleds[EFFEKT_LED_NUM];

DeviceState KoKriRec_State = State_INITIALIZING;

Button recordButton(RECORD_BUTTON_PIN, BUTTON_DEBOUNCE_TIME);
//Button LadenschalenKontakt(LADESCHALEN_KONTAKT_PIN, BUTTON_DEBOUNCE_TIME);

volatile BlinkState currentBlinkState = BLINK_NONE;
volatile bool ledBlinkState = true;

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("ESP32-S3 Audio Recorder (16kHz) mit WS2812-LED");
  
  // WS2812-LED initialisieren
  initLED();
  setLEDStatus(CRGB(64, 64, 0)); // Gelb während der Initialisierung
  
  // Button mit Pull-up-Widerstand
  pinMode(RECORD_BUTTON_PIN, INPUT_PULLUP);
  //pinMode(LADESCHALEN_KONTAKT_PIN, INPUT_PULLUP);

  // Erstelle Semaphore für SD-Karten-Zugriff
  sdCardMutex = xSemaphoreCreateMutex();
  
  // Erstelle Queue für Upload-Tasks
  uploadQueue = xQueueCreate(20, MAX_FILENAME_LEN * sizeof(char));
  
  // Erstelle Queue für Audio-Daten
  audioQueue = xQueueCreate(AUDIO_QUEUE_LENGTH, sizeof(struct AudioData));
  if (audioQueue == NULL) {
      Serial.println("Error creating audio queue");
  }
  
  // I2S und SD-Karte initialisieren
  bool sdOk = initSDCard();
  bool micOk = initI2S();
  bool configReadOk = loadConfigFromSD();

  if (!micOk || !sdOk || !configReadOk || audioQueue == NULL) {
    // Mindestens eine Komponente hat Fehler
    Serial.println("Initialisierung fehlgeschlagen.");
    
    // Zeige Fehler mit speziellem Blinkmuster
    while (true) {
      if (!micOk || audioQueue == NULL){
        setLEDStatus(CRGB(255, 0, 0));  // Rot für Mikrofon-Fehler
        delay(500);
      }else if (!sdOk){
        setLEDStatus(CRGB(0, 0, 255));  // Blau für SD-Fehler
        delay(500);
      }else if(!configReadOk){
        setLEDStatus(CRGB(0, 255, 255)); // Türkis für Config Lese fehler
        delay(500);
      }     
    }
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

  // Start WiFi control task
  xTaskCreate(
    WiFiControlTask,
    "WiFi Control Task",
    8192,
    NULL,
    UPLOAD_TASK_PRIORITY,  // Same priority as FTP task
    NULL
  );
  
  while(WiFi.status() != WL_CONNECTED) {
    Serial.println("WLAN nicht verbunden. Warte auf Verbindung...");
    delay(500);
    setLEDStatus(CRGB::Black);
    delay(500);
    setLEDStatus(CRGB::Yellow);
  }

  if(config.webserverEnabled){
    initWebServer();
  }

  // Set LED to ready status
  setLEDStatus(COLOR_IDLE);

  KoKriRec_State = State_IDLE;
  Serial.println("Recorder bereit. Drücke den Button, um die Aufnahme zu starten/stoppen.");
}

// Füge State-Tracking hinzu
static DeviceState lastState = State_INITIALIZING;

void loop() {
  // Update blink state based on queue status
  if (uxQueueMessagesWaiting(uploadQueue) > 0) {
    currentBlinkState = BLINK_SLOW;
  } else {
    currentBlinkState = BLINK_NONE;
  }

  updateStatusBlink();

  if (WiFi.status() == WL_CONNECTED) {
    if (currentBlinkState != BLINK_NONE) {
      setLEDStatus(ledBlinkState ? CRGB::Black : CRGB::Green);
    } else {
      setLEDStatus(CRGB::Green);
    }
  } else {
    if (currentBlinkState != BLINK_NONE) {
      setLEDStatus(ledBlinkState ? CRGB::Black : CRGB::Yellow);
    } else {
      setLEDStatus(CRGB::Yellow);
    }
  }

  // Prüfe auf State-Änderung
  if (lastState != KoKriRec_State) {
    // Aktualisiere Farben nur bei State-Änderung
    switch (KoKriRec_State) {
      case State_IDLE:
        updateStateColor(96, 240, 40, 200);  // Grün
        break;
      case State_RECORDING:
        updateStateColor(0, 255, 40, 200);   // Rot
        break;
      case State_KOKRI_SCHALE_UPLOADING:
        updateStateColor(160, 255, 40, 200); // Türkis
        break;
      case State_KOKRI_SCHALE_IDLE:
        updateStateColor(190, 255, 40, 200); // Blau-Türkis
        break;
      case State_RECORDING_ERROR:
      case State_SD_ERROR:
      case State_FTP_ERROR:
      default:
      case State_ERROR:
        updateStateColor(0, 255, 40, 150);   // Gedämpftes Rot
        break;
    }
    lastState = KoKriRec_State;
  }

  // State-spezifische Logik ohne Farbaktualisierung
  switch (KoKriRec_State) {
    case State_IDLE:

      updateAnimation(2);
      if (recordButton.isPressed()) {
        KoKriRec_State = State_RECORDING;
        startRecording();
        break;
      }
      /*
      if (LadenschalenKontakt.isPressed()) {
        KoKriRec_State = State_KOKRI_SCHALE_UPLOADING;
      }
      */
      break;

    case State_RECORDING:
      updateAnimation((int)(smoothedAudioLevel));
      if (!recordButton.isPressed()) {
        KoKriRec_State = State_IDLE;
        vTaskDelay(pdMS_TO_TICKS(50));
      }
      break;

    case State_KOKRI_SCHALE_UPLOADING:
      updateAnimation(2);
      if(uxQueueMessagesWaiting(uploadQueue) == 0) {
        KoKriRec_State = State_KOKRI_SCHALE_IDLE;
      }
      break;

    case State_KOKRI_SCHALE_IDLE:
      updateAnimation(2);
      /*if (!LadenschalenKontakt.isPressed()) {
        KoKriRec_State = State_IDLE;
      }*/
      break;

    case State_RECORDING_ERROR:
    case State_SD_ERROR:
    case State_FTP_ERROR:
    default:
    case State_ERROR:

      break;
  }
  
  vTaskDelay(pdMS_TO_TICKS(10));
}