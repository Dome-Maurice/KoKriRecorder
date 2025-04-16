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
Button LadenschalenKontakt(LADESCHALEN_KONTAKT_PIN, BUTTON_DEBOUNCE_TIME);

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("ESP32-S3 Audio Recorder (16kHz) mit WS2812-LED");
  
  // WS2812-LED initialisieren
  initLED();
  setLEDStatus(CRGB(64, 64, 0)); // Gelb während der Initialisierung
  
  // Button mit Pull-up-Widerstand
  pinMode(RECORD_BUTTON_PIN, INPUT_PULLUP);
  pinMode(LADESCHALEN_KONTAKT_PIN, INPUT_PULLUP);

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

  if(config.webserverEnabled){
    initWebServer();
  }

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
  
  // Set LED to ready status
  setLEDStatus(COLOR_IDLE);

  KoKriRec_State = State_IDLE;
  Serial.println("Recorder bereit. Drücke den Button, um die Aufnahme zu starten/stoppen.");
}

byte hue = 0;

void loop() {

    switch (KoKriRec_State){
    case State_IDLE:
    
      if (recordButton.isPressed()) {
        KoKriRec_State = State_RECORDING;
        startRecording();
        break;
      }

      if (LadenschalenKontakt.isPressed()) {
        KoKriRec_State = State_KOKRI_SCHALE_UPLOADING;
      }

      sinelon(hue);

      break;

    case State_RECORDING:

      if (!recordButton.isPressed()) {
        KoKriRec_State = State_IDLE; // Aufnahme Task wird sich selbst beenden
        vTaskDelay(pdMS_TO_TICKS(50)); // Warte auf Abschluss der Aufnahme
      }

      break;

    case State_KOKRI_SCHALE_UPLOADING:

      idle_Animation(COLOR_KRISTALL_UPLOADING, 500);

      if(uxQueueMessagesWaiting(uploadQueue) == 0){
        KoKriRec_State = State_KOKRI_SCHALE_IDLE;
      }

      //FTP Datei Erstellen wenn zu Signalisiserung das fertig geuploaded ist

      break;

    case State_KOKRI_SCHALE_IDLE:

      idle_Animation(COLOR_KRISTALL_IDLE, 500);
      if (!LadenschalenKontakt.isPressed()) {
        KoKriRec_State = State_IDLE;
      }

      break;

    case State_RECORDING_ERROR:
    case State_SD_ERROR:
    case State_FTP_ERROR:
    default:
    case State_ERROR:
      idle_Animation(COLOR_ERROR, 100);
      break;
    }

    hue++;
    vTaskDelay(pdMS_TO_TICKS(10));

}