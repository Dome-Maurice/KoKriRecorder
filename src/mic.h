#ifndef MIC_H
#define MIC_H

#include <Arduino.h>
#include <driver/i2s.h>
#include "config.h"
#include "recording.h"  // Neue Header-Datei für gemeinsame Funktionen

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

// Funktion zum Lesen von Audiodaten vom Mikrofon
esp_err_t readMicrophoneData(int32_t* samples, size_t* bytesRead) {
  return i2s_read(I2S_PORT, samples, BUFFER_SIZE * sizeof(int32_t), bytesRead, portMAX_DELAY);
}

#endif // MIC_H