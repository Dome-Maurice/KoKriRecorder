#ifndef FTP_H
#define FTP_H

#include <Arduino.h>
#include "config.h"
#include "led.h"

extern bool isRecording;
extern SemaphoreHandle_t sdCardMutex;
extern QueueHandle_t uploadQueue;

void uploadTask(void* parameter) {
    char uploadFilename[MAX_FILENAME_LEN];
    
    while (true) {
        if (xQueueReceive(uploadQueue, uploadFilename, portMAX_DELAY) == pdTRUE) {
            Serial.printf("Queuing für Upload: %s\n", uploadFilename);
            
            if (!isRecording) {
                setLEDStatus(COLOR_UPLOAD);
            }
            
            Serial.printf("Simuliere Upload von %s...\n", uploadFilename);
            
            if (xSemaphoreTake(sdCardMutex, portMAX_DELAY) == pdTRUE) {
                File fileToUpload = SD.open(uploadFilename);
                
                if (fileToUpload) {
                    uint32_t fileSize = fileToUpload.size();
                    Serial.printf("Datei zum Upload: %s, Größe: %u kB\n", uploadFilename, fileSize/1000);
                    
                    xSemaphoreGive(sdCardMutex);
                    
                    uint32_t simulatedUploadTime = (fileSize / 1024) + 1000;
                    Serial.printf("Simuliere Upload für %u ms...\n", simulatedUploadTime);
                    
                    for (uint32_t i = 0; i < simulatedUploadTime && !isRecording; i += 100) {
                        if (!isRecording) {
                            uint8_t brightness = 50 + 50 * sin(i * 0.01);
                            leds[0] = COLOR_UPLOAD;
                            leds[0].fadeToBlackBy(255 - brightness);
                            FastLED.show();
                        }
                        vTaskDelay(pdMS_TO_TICKS(100));
                    }
                    
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
            
            if (!isRecording) {
                setLEDStatus(COLOR_READY);
            }
        }
    }
}

#endif // FTP_H