#ifndef MIC_H
#define MIC_H

#include <Arduino.h>
#include <driver/i2s.h>
#include "config.h"
//#include "recording.h"  // Neue Header-Datei für gemeinsame Funktionen
#include "freertos/queue.h"
#include "driver/i2s.h"

// Debug flags am Anfang der Datei nach den includes
static volatile uint32_t isr_call_count = 0;
static volatile uint32_t isr_bytes_read = 0;
static volatile uint32_t isr_queue_full_count = 0;
static volatile uint32_t isr_last_error = ESP_OK;

struct AudioData {
  int32_t samples[BUFFER_SIZE];
  size_t bytesRead;
};

// ISR-safe Queue für Audio Daten
static portMUX_TYPE audioBufMux = portMUX_INITIALIZER_UNLOCKED;
static QueueHandle_t audioQueue = NULL;

// Add interrupt handle storage
static intr_handle_t i2s_interrupt_handle = NULL;

// I2S Interrupt Handler
static void IRAM_ATTR i2s_isr_handler(void* arg) {
    static struct AudioData audioData;
    size_t bytesRead = 0;
    BaseType_t high_task_wakeup = pdFALSE;

    isr_call_count++;  // Zähle Interrupt-Aufrufe

    portENTER_CRITICAL_ISR(&audioBufMux);
    
    esp_err_t result = i2s_read(I2S_PORT, audioData.samples, 
                               BUFFER_SIZE * sizeof(int32_t), 
                               &bytesRead, 0);
    
    if (result == ESP_OK && bytesRead > 0) {
        audioData.bytesRead = bytesRead;
        isr_bytes_read += bytesRead;  // Summiere gelesene Bytes
        
        if (xQueueSendFromISR(audioQueue, &audioData, &high_task_wakeup) != pdTRUE) {
            isr_queue_full_count++;  // Zähle Queue-Überläufe
        }
    } else {
        isr_last_error = result;  // Speichere letzten Fehler
    }
    
    portEXIT_CRITICAL_ISR(&audioBufMux);
    
    if (high_task_wakeup == pdTRUE) {
        portYIELD_FROM_ISR();
    }
}

// Enable I2S interrupt
static esp_err_t enable_i2s_interrupt() {
    if (i2s_interrupt_handle == NULL) {
        esp_err_t err = esp_intr_alloc(ETS_I2S0_INTR_SOURCE, 
                                      ESP_INTR_FLAG_IRAM, 
                                      i2s_isr_handler, 
                                      NULL, 
                                      &i2s_interrupt_handle);
        if (err == ESP_OK) {
            Serial.println("I2S Interrupt erfolgreich registriert");
        } else {
            Serial.printf("Fehler bei Interrupt-Registrierung: %d\n", err);
        }
        return err;
    }
    return ESP_OK;
}

// Disable I2S interrupt
static esp_err_t disable_i2s_interrupt() {
    if (i2s_interrupt_handle != NULL) {
        esp_err_t err = esp_intr_free(i2s_interrupt_handle);
        if (err == ESP_OK) {
            i2s_interrupt_handle = NULL;
        }
        return err;
    }
    return ESP_OK;
}


// Modifizierte I2S Initialisierung
bool initI2S() {
    i2s_config_t i2s_config = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
        .sample_rate = SAMPLE_RATE,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,
        .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags = ESP_INTR_FLAG_IRAM,
        .dma_buf_count = 8,
        .dma_buf_len = BUFFER_SIZE,
        .use_apll = true,
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
    
    // I2S RX explizit starten
    err = i2s_start(I2S_PORT);
    if (err != ESP_OK) {
        Serial.println("Fehler beim Starten des I2S RX");
        return false;
    }
    
    Serial.println("I2S-Mikrofon initialisiert");
    return true;
}

// Funktion zum Auslesen der Debug-Informationen
void printI2SDebugInfo() {
    static uint32_t last_print = 0;
    if (millis() - last_print >= 1000) {  // Einmal pro Sekunde
        Serial.printf("I2S ISR Stats:\n");
        Serial.printf("- Aufrufe: %lu\n", isr_call_count);
        Serial.printf("- Bytes gelesen: %lu\n", isr_bytes_read);
        Serial.printf("- Queue voll: %lu mal\n", isr_queue_full_count);
        Serial.printf("- Letzter Fehler: %d\n", isr_last_error);
        
        // Optionaler Reset der Zähler
        isr_call_count = 0;
        isr_bytes_read = 0;
        isr_queue_full_count = 0;
        
        last_print = millis();
    }
}

#endif // MIC_H