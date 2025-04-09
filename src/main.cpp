#include <Arduino.h>
#include <driver/i2s.h>
#include <SD.h>
#include <FS.h>
#include <SPI.h>
#include <FastLED.h>  // FastLED-Bibliothek für WS2812-LED
#include <WiFi.h>
#include <ESPAsyncWebServer.h>

// Konstanten für I2S
#define I2S_WS_PIN      12       // Word Select (WS) Pin
#define I2S_SCK_PIN     13       // Serial Clock (SCK) Pin
#define I2S_SD_PIN      11       // Serial Data (SD) Pin
#define I2S_PORT        I2S_NUM_0
#define SAMPLE_RATE     16000    // Sample Rate in Hz (16kHz)
#define BUFFER_SIZE     1024     // Größe des Aufnahmepuffers
#define BIT_DEPTH       32       // INMP441 liefert 24-Bit Daten, I2S empfängt als 32-Bit

// Konstanten für SD-Karte
#define SD_CS_PIN       4        // SD Card Chip Select Pin
#define SD_MOSI_PIN     6        // SD Card MOSI
#define SD_MISO_PIN     7        // SD Card MISO
#define SD_SCK_PIN      5        // SD Card SCK

// Button für Aufnahmesteuerung
#define RECORD_BUTTON_PIN 9      // Button-Pin für Aufnahmesteuerung
#define MAX_FILENAME_LEN 32      // Maximale Länge des Dateinamens

// WS2812 LED-Konfiguration
#define LED_PIN         48        // Pin für die WS2812-LED
#define NUM_LEDS        1        // Nur eine LED
#define LED_TYPE        WS2812   // LED-Typ
#define COLOR_ORDER     GRB      // Farbreihenfolge (meistens GRB bei WS2812)

// Status-Farben
#define COLOR_READY     CRGB(0, 64, 0)    // Grün (gedimmt) = Bereit
#define COLOR_RECORDING CRGB(255, 0, 0)   // Rot = Aufnahme
#define COLOR_ERROR     CRGB(255, 50, 0)  // Orange = Fehler
#define COLOR_UPLOAD    CRGB(0, 0, 255)   // Blau = Daten werden hochgeladen

// Task-Prioritäten
#define RECORDING_TASK_PRIORITY 10  // Hohe Priorität für Aufnahme-Task
#define UPLOAD_TASK_PRIORITY 5      // Niedrigere Priorität für Upload-Task

// Globale Variablen
bool isRecording = false;        // Aufnahmestatus
uint32_t recordingStartTime = 0; // Startzeit der Aufnahme
char filename[MAX_FILENAME_LEN]; // Dateiname für die WAV-Datei
File wavFile;                    // Datei-Handle für WAV-Datei
unsigned long fileSize = 0;      // Größe der Datei (für WAV-Header)
unsigned long dataSize = 0;      // Größe der Audio-Daten
CRGB leds[NUM_LEDS];             // Array für WS2812-LED

const char* tempFilename = "/recording.tmp";

// Queue für FTP-Upload-Tasks
QueueHandle_t uploadQueue;
SemaphoreHandle_t sdCardMutex;   // Mutex für SD-Karten-Zugriff

#include <readconfig.h>

AsyncWebServer server(80);

String urlDecode(const String& input) {
  String decoded = "";
  char temp[] = "0x00";
  unsigned int len = input.length();
  unsigned int i = 0;

  while (i < len) {
    char c = input.charAt(i);
    if (c == '+') {
      decoded += ' ';
    } else if (c == '%' && i + 2 < len) {
      temp[2] = input.charAt(i + 1);
      temp[3] = input.charAt(i + 2);
      decoded += (char)strtol(temp, NULL, 16);
      i += 2;
    } else {
      decoded += c;
    }
    i++;
  }
  return decoded;
}


void setupWebServer() {
  // Hauptseite: Listet alle Dateien auf
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    String html = "<h2>Aufnahmen:</h2><ul>";
    
    if (xSemaphoreTake(sdCardMutex, pdMS_TO_TICKS(500)) == pdTRUE) {
      File root = SD.open("/");
      File file = root.openNextFile();
      
      while (file) {
        String fname = String(file.name());
        html += "<li><a href=\"/" + fname + "\">" + fname + "</a> <a href=\"/delete?file=" + fname + "\">[Delete]</a></li>";
        file = root.openNextFile();
      }
      
      xSemaphoreGive(sdCardMutex);
    } else {
      html = "SD-Karte ist gerade belegt. Bitte später versuchen.";
    }
    
    html += "</ul>";
    request->send(200, "text/html", html);
  });

  // Handler für direkten Dateidownload
  server.onNotFound([](AsyncWebServerRequest *request){
    String path = request->url();
    if (!SD.exists(path)) {
      request->send(404, "text/plain", "Datei nicht gefunden");
      return;
    }
    
    if (xSemaphoreTake(sdCardMutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
      File file = SD.open(path);
      request->send(file, path, "application/octet-stream");
      xSemaphoreGive(sdCardMutex);
    } else {
      request->send(503, "text/plain", "SD-Karte momentan nicht verfuegbar");
    }
  });

  server.on("/delete", HTTP_GET, [](AsyncWebServerRequest *request){
    if (!request->hasParam("file")) {
      request->send(400, "text/plain", "Dateiparameter fehlt.");
      return;
    }
  
    String fileToDelete = request->getParam("file")->value();
  
    // Optional: URL-dekodieren (für Sonderzeichen etc.)
    fileToDelete = urlDecode(fileToDelete);
  
    // Sicherstellen, dass Pfad mit / beginnt
    if (!fileToDelete.startsWith("/")) {
      fileToDelete = "/" + fileToDelete;
    }
  
    Serial.printf("Lösche Datei: %s\n", fileToDelete.c_str());
  
    if (xSemaphoreTake(sdCardMutex, pdMS_TO_TICKS(500)) == pdTRUE) {
      if (SD.exists(fileToDelete)) {
        if (SD.remove(fileToDelete)) {
          xSemaphoreGive(sdCardMutex);
          request->send(200, "text/plain", "Datei geloescht.");
        } else {
          xSemaphoreGive(sdCardMutex);
          request->send(500, "text/plain", "Fehler beim Loeschen der Datei.");
        }
      } else {
        xSemaphoreGive(sdCardMutex);
        request->send(404, "text/plain", "Datei nicht gefunden.");
      }
    } else {
      request->send(503, "text/plain", "SD-Karte momentan nicht verfuegbar.");
    }
  });
  
  

  server.begin();
  Serial.println("Webserver gestartet.");
}



void connectWiFi() {
  WiFi.begin(config.wifiSSID, config.wifiPassword);
  Serial.print("Verbinde mit WLAN");

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("\nWLAN verbunden!");
  Serial.print("IP-Adresse: ");
  Serial.println(WiFi.localIP());
}

// Prototypen
bool initI2S();
bool initSDCard();
void initLED();
bool startRecording();
void stopRecording();
void writeWAVHeader();
void updateWAVHeader();
void handleButton();
void setLEDStatus(CRGB color);
void pulseLED(CRGB color);
void recordingTask(void* parameter);
void uploadTask(void* parameter);

// WAV-Header Struktur
struct WAVHeader {
  // RIFF Header
  char riffHeader[4] = {'R', 'I', 'F', 'F'}; // RIFF Header Magic
  uint32_t wavSize = 0;                       // RIFF Chunk Size
  char waveHeader[4] = {'W', 'A', 'V', 'E'}; // WAVE Header
  
  // Format Header
  char fmtHeader[4] = {'f', 'm', 't', ' '};  // FMT Header
  uint32_t fmtChunkSize = 16;                // FMT Chunk Size
  uint16_t audioFormat = 1;                  // Audio Format (1 = PCM)
  uint16_t numChannels = 1;                  // Anzahl der Kanäle (1 = Mono)
  uint32_t sampleRate = SAMPLE_RATE;         // Sample Rate
  uint32_t byteRate = SAMPLE_RATE * 2;       // Byte Rate (SampleRate * NumChannels * BitsPerSample/8)
  uint16_t blockAlign = 2;                   // Block Alignment (NumChannels * BitsPerSample/8)
  uint16_t bitsPerSample = 16;               // Bits pro Sample (16 für PCM)
  
  // Data Header
  char dataHeader[4] = {'d', 'a', 't', 'a'}; // DATA Header
  uint32_t dataChunkSize = 0;                // Größe des Datenchunks
};

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
    connectWiFi();
    setupWebServer();
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

  return true; 
}

bool startRecording() {
  
  Serial.printf("Starte Aufnahme: %s\n", tempFilename);

  if (xSemaphoreTake(sdCardMutex, portMAX_DELAY) == pdTRUE) {
    // Öffne die Datei zum Schreiben
    wavFile = SD.open(tempFilename, FILE_WRITE);

    
    if (!wavFile) {
      Serial.println("Fehler beim Öffnen der temporären Datei!");
      setLEDStatus(COLOR_ERROR);
      xSemaphoreGive(sdCardMutex);
      return false;
    }
    
    // WAV-Header schreiben
    writeWAVHeader();
    xSemaphoreGive(sdCardMutex);
    
    dataSize = 0;
    recordingStartTime = millis();
    isRecording = true;
    
    // LED auf Aufnahme-Status setzen
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
    
    return true;
  }
  
  return false;
}

void stopRecording() {
  if (isRecording) {
    isRecording = false;
    // Der Aufnahme-Task wird sich selbst beenden, wenn isRecording false ist
    
    // Warte kurz, um sicherzustellen, dass der Aufnahme-Task abgeschlossen ist
    delay(100);
    
    // LED auf Bereit-Status zurücksetzen
    setLEDStatus(COLOR_READY);
  }
}

void writeWAVHeader() {
  // WAV-Header erstellen
  WAVHeader header;
  
  // RIFF-Chunk-Größe und Daten-Chunk-Größe werden später aktualisiert
  
  // Schreibe Header in die Datei
  wavFile.write((const uint8_t *)&header, sizeof(WAVHeader));
}

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

// Task für Audioaufnahme (läuft mit hoher Priorität)
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

    //SD.rename(tempFilename, filename);
    //if(SD.rename("/config.txt", "/oldConfig.txt")){
    //  Serial.printf("Aufnahme gespeichert als: %s\n", filename);
    //}else{
    //  Serial.printf("Rename failed !\n");
    //}

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

// TODO: FTP-Upload-Funktion (für später)
/*
void uploadFileToFTP(const char* fileToUpload) {
  // Hier kommt der Code für den FTP-Upload
  // Implementiere dies mit einer ESP32-kompatiblen FTP-Client-Bibliothek
  // wie z.B. ESP32_FTPClient
  
  // Beispiel:
  // ESP32_FTPClient ftp(ftp_server, ftp_user, ftp_password, ftp_port);
  // ftp.OpenConnection();
  // ftp.ChangeWorkDir("/upload");
  // ftp.InitFile("Type I");
  // ftp.NewFile(basename(fileToUpload));
  // 
  // File f = SD.open(fileToUpload, FILE_READ);
  // if (f) {
  //   uint8_t buffer[1024];
  //   while (int bytesRead = f.read(buffer, sizeof(buffer))) {
  //     ftp.WriteData(buffer, bytesRead);
  //   }
  //   f.close();
  // }
  // 
  // ftp.CloseFile();
  // ftp.CloseConnection();
}
*/