#ifndef FTP_H
#define FTP_H

#include <Arduino.h>
#include <ESP32_FTPClient.h>
#include "config.h"
#include "led.h"

extern bool isRecording;
extern SemaphoreHandle_t sdCardMutex;
extern QueueHandle_t uploadQueue;



void FTPuploadTask(void* parameter) {

    char uploadFilename[MAX_FILENAME_LEN];
    char tempFilename[MAX_FILENAME_LEN+5];
    ESP32_FTPClient ftpclient(config.ftpServer, config.ftpPort, config.ftpUser, config.ftpPassword, FTP_TIMEOUT, 1);
    
    while (WiFi.status() == WL_CONNECTED) {
        if (xQueuePeek(uploadQueue, uploadFilename, portMAX_DELAY) == pdTRUE) {
            Serial.printf("Queuing für Upload: %s\n", uploadFilename);
            snprintf(tempFilename, sizeof(tempFilename), "/%s.temp", uploadFilename);
            bool uploadSuccess = false;

            if (xSemaphoreTake(sdCardMutex, portMAX_DELAY) == pdTRUE) {
                File fileToUpload = SD.open(uploadFilename);
                
                if (!isRecording) {
                    setLEDStatus(COLOR_UPLOAD);
                }

                if (fileToUpload) {
                    uint32_t fileSize = fileToUpload.size();
                    Serial.printf("Datei zum Upload: %s, Größe: %u kB\n", uploadFilename, fileSize/1000);  

                    xSemaphoreGive(sdCardMutex);

                    if(!ftpclient.isConnected()) {
                        ftpclient.OpenConnection();
                    }

                    ftpclient.InitFile("Type I");
                    ftpclient.NewFile(tempFilename);
                    
                    uint32_t bytesUploaded = 0;
                    uint8_t buffer[FTP_BUFFER_SIZE];             

                    while (bytesUploaded < fileSize) {
                        if (xSemaphoreTake(sdCardMutex, portMAX_DELAY) == pdTRUE) {
                            size_t bytesRead = fileToUpload.read(buffer, sizeof(buffer));
                            xSemaphoreGive(sdCardMutex);  
                            ftpclient.WriteData(buffer, bytesRead);                       
                            bytesUploaded += bytesRead;           
                        }

                        if(ftpclient.isConnected() == false){
                            Serial.println("FTP Verbindung verloren. Upload abgebrochen.");
                            break;
                        }

                        vTaskDelay(pdMS_TO_TICKS(1));
                    }
                    
                    ftpclient.CloseFile();
                    fileToUpload.close();
                    
                    if(ftpclient.isConnected() && bytesUploaded == fileSize) {
                        uploadSuccess = true;
                        Serial.printf("Upload von %s abgeschlossen.\n", uploadFilename);
                        ftpclient.RenameFile(tempFilename, uploadFilename);
                        xQueueReceive(uploadQueue, uploadFilename, 0);
                    }

                } else {
                    xSemaphoreGive(sdCardMutex);
                    Serial.printf("Fehler beim Öffnen der Datei für Upload: %s\n", uploadFilename);                    
                }
            }
            
            if(!uploadSuccess) {
                vTaskDelay(pdMS_TO_TICKS(5000));
                Serial.printf("Upload fehlgeschlagen. Datei %s wird erneut in die Warteschlange gestellt.\n", uploadFilename);
            }

            if(uxQueueMessagesWaiting(uploadQueue) == 0) {
                ftpclient.CloseConnection();
                Serial.println("Keine weiteren Uploads in der Warteschlange.");
            }
        }
    }
    vTaskDelete(NULL);
}



#endif // FTP_H