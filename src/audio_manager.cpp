#include "audio_manager.h"

// Global variable definitions
QueueHandle_t audioQueue = NULL;
char filename[MAX_FILENAME_LEN];
File wavFile;
unsigned long dataSize = 0;
uint32_t recordingStartTime = 0;

volatile int currentAudioLevel = 0;
volatile int peakAudioLevel = 0;
volatile float smoothedAudioLevel = 0;

bool initI2S() {
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

    i2s_pin_config_t pin_config = {
        .bck_io_num = I2S_SCK_PIN,
        .ws_io_num = I2S_WS_PIN,
        .data_out_num = I2S_PIN_NO_CHANGE,
        .data_in_num = I2S_SD_PIN
    };

    esp_err_t err = i2s_driver_install(I2S_PORT, &i2s_config, 0, NULL);
    if (err != ESP_OK) {
        Serial.printf("Fehler bei der I2S-Treiberinstallation: %d\n", err);
        
        switch(err) {
            case ESP_ERR_INVALID_ARG:
                Serial.println("Ungültige Argumente für i2s_driver_install");
                break;
            case ESP_ERR_NO_MEM:
                Serial.println("Nicht genügend Speicher für I2S-Treiber");
                break;
            case ESP_ERR_INVALID_STATE:
                Serial.println("I2S-Treiber bereits installiert");
                i2s_driver_uninstall(I2S_PORT);
                err = i2s_driver_install(I2S_PORT, &i2s_config, 0, NULL);
                if (err != ESP_OK) {
                    Serial.println("Erneute Installation fehlgeschlagen");
                    KoKriRec_State = State_ERROR;
                    return false;
                }
                Serial.println("I2S-Treiber neu installiert");
                break;
            default:
                Serial.println("Unbekannter I2S-Treiberfehler");
                break;
        }
        
        if (err != ESP_OK) {
            KoKriRec_State = State_ERROR;
            return false;
        }
    }

    err = i2s_set_pin(I2S_PORT, &pin_config);
    if (err != ESP_OK) {
        Serial.printf("Fehler bei der I2S-Pin-Konfiguration: %d\n", err);
        
        switch(err) {
            case ESP_ERR_INVALID_ARG:
                Serial.println("Ungültige Pin-Konfiguration");
                Serial.printf("Prüfe Pins: SCK=%d, WS=%d, SD=%d\n", I2S_SCK_PIN, I2S_WS_PIN, I2S_SD_PIN);
                break;
            default:
                Serial.println("Unbekannter Pin-Konfigurationsfehler");
                break;
        }
        
        i2s_driver_uninstall(I2S_PORT);
        KoKriRec_State = State_ERROR;
        return false;
    }

    // Queue erstellen falls noch nicht existiert
    if (audioQueue == NULL) {
        audioQueue = xQueueCreate(AUDIO_QUEUE_LENGTH, sizeof(struct AudioData));
        if (audioQueue == NULL) {
            Serial.println("Fehler beim Erstellen der Audio Queue");
            KoKriRec_State = State_ERROR;
            return false;
        }
    }

    Serial.println("I2S-Mikrofon initialisiert");
    return true;
}

esp_err_t readMicrophoneData(int32_t* samples, size_t* bytesRead) {
    return i2s_read(I2S_PORT, samples, BUFFER_SIZE * sizeof(int32_t), bytesRead, portMAX_DELAY);
}

void recordingTask(void* parameter) {
    struct AudioData audioData;
    Serial.println("Aufnahme-Task gestartet");

    do {
        if (xQueueReceive(audioQueue, &audioData, pdMS_TO_TICKS(1)) == pdTRUE) {
            int16_t pcmData[BUFFER_SIZE];
            int32_t sum = 0;
            int32_t peak = 0;

            for (int i = 0; i < audioData.bytesRead / 4; i++) {
                int32_t sample = audioData.samples[i] >> 8;
                int32_t absSample = abs(sample);
                sum += absSample;

                if (absSample > peak) {
                    peak = absSample;
                }

                if (sample > 32767) sample = 32767;
                if (sample < -32768) sample = -32768;
                
                pcmData[i] = (int16_t)sample;
            }

            size_t bytesToWrite = audioData.bytesRead / 2;
            writeAudioDataToSD(pcmData, bytesToWrite);

            // Aktualisiere globale Audio-Level
            currentAudioLevel = constrain((sum / (audioData.bytesRead / 4)) >> AUDIO_SCALE_FACTOR, 0, 255);
            peakAudioLevel = constrain(peak >> AUDIO_SCALE_FACTOR, 0, 255);
            smoothedAudioLevel = (smoothedAudioLevel * AUDIO_SMOOTHING_FACTOR) + 
                               (currentAudioLevel * (1.0f - AUDIO_SMOOTHING_FACTOR));
        }
    } while (KoKriRec_State == State_RECORDING || uxQueueMessagesWaiting(audioQueue));

    finalizeRecordingFile();
    vTaskDelete(NULL);
}

void microphoneTask(void* parameter) {
    struct AudioData audioData;
    
    while (KoKriRec_State == State_RECORDING) {
        esp_err_t result = readMicrophoneData(audioData.samples, &audioData.bytesRead);
        
        if (result == ESP_OK && audioData.bytesRead > 0) {
            if (xQueueSend(audioQueue, &audioData, 0) != pdTRUE) {
                Serial.println("Queue voll!");
            }
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    vTaskDelete(NULL);
}