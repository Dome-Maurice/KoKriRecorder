#ifndef FTP_H
#define FTP_H

#include <Arduino.h>
#include <ESP32_FTPClient.h>
#include "config.h"
#include "led.h"

extern SemaphoreHandle_t sdCardMutex;

QueueHandle_t uploadQueue;

// Minimum signal strength threshold in dBm
const int MIN_RSSI = -70;  // Adjust this value as needed (-70 dBm is a good starting point)

// Function to get signal quality description
String getSignalQuality(int rssi) {
    if (rssi >= -50) return "#####";
    if (rssi >= -60) return "#### ";
    if (rssi >= -70) return "###  ";
    if (rssi >= -80) return "##   ";
    return                  "#    ";
}

// Function to calculate signal quality percentage
int calculateSignalQuality(int rssi) {
    // RSSI range is typically -100 dBm (0%) to -50 dBm (100%)
    if (rssi >= -50) return 100;
    if (rssi <= -100) return 0;
    return 2 * (rssi + 100);  // Linear conversion from -100..-50 to 0..100
}

void scanNetworks() {
    Serial.print("\n### WiFi Network Scan: ");
    int n = WiFi.scanNetworks();
    if (n == 0) {
        Serial.println("found no networks.");
    } else {
        Serial.printf("found %d networks:\n", n);
        for (int i = 0; i < n; ++i) {
            int rssi = WiFi.RSSI(i);
            int quality = calculateSignalQuality(rssi);
            String qualityDesc = getSignalQuality(rssi);
            
            // Print network information
            Serial.printf("%2d: ", i + 1);
            Serial.printf("%32s ", WiFi.SSID(i));
            Serial.printf("%4d dBm (%3d%%) %s ", rssi, quality, qualityDesc.c_str());
            Serial.printf("chan %2d ", WiFi.channel(i));
            Serial.printf("encr %s", 
                (WiFi.encryptionType(i) == WIFI_AUTH_OPEN) ? "Open" : 
                (WiFi.encryptionType(i) == WIFI_AUTH_WEP) ? "WEP" :
                (WiFi.encryptionType(i) == WIFI_AUTH_WPA_PSK) ? "WPA_PSK" :
                (WiFi.encryptionType(i) == WIFI_AUTH_WPA2_PSK) ? "WPA2_PSK" :
                (WiFi.encryptionType(i) == WIFI_AUTH_WPA_WPA2_PSK) ? "WPA/WPA2_PSK" : "Unknown");
            Serial.println("");  // Blank line between networks
        }
        for (int i = 0; i < n; ++i) {
            if (WiFi.SSID(i) == config.wifiSSID) {
                Serial.print((String)"### Target network '" + config.wifiSSID + "' found ");
                int rssi = WiFi.RSSI(i);
                if (rssi >= MIN_RSSI) {
                    Serial.println(" -- attempting to connect ");
                    WiFi.begin(config.wifiSSID, config.wifiPassword);
                   
                    int attempts = 0;
                    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
                        vTaskDelay(pdMS_TO_TICKS(500));
                        Serial.print(".");
                        attempts++;
                    }
                    
                    if (WiFi.status() == WL_CONNECTED) {
                        Serial.println(" -- connection successful ");
                        Serial.printf("connected to: %s\n", config.wifiSSID);
                        Serial.printf("IP address: %s\n", WiFi.localIP().toString().c_str());
                        Serial.printf("signal strength: %d dBm (%d%%) - %s\n", 
                            WiFi.RSSI(), calculateSignalQuality(WiFi.RSSI()), 
                            getSignalQuality(WiFi.RSSI()).c_str());
                        Serial.printf("chan %d\n", WiFi.channel());
                        return;
                    } else {
                        Serial.println(" -- connection failed.");
                        WiFi.disconnect();
                    }
                } else {
                    Serial.printf("    signal too weak for connection\n");
                    Serial.printf("    minimum required: %d dBm, current: %d dBm\n", MIN_RSSI, rssi);
                }
            }
        }
    }
}

void FTPuploadTask(void* parameter) {
    char uploadFilename[MAX_FILENAME_LEN];
    char tempFilename[MAX_FILENAME_LEN+5];
    ESP32_FTPClient ftpclient(config.ftpServer, config.ftpPort, config.ftpUser, config.ftpPassword, FTP_TIMEOUT, 1);
    
    while (true) {
        if (WiFi.status() == WL_CONNECTED) {

            if(!ftpclient.isConnected()) {
                ftpclient.OpenConnection();
                vTaskDelay(pdMS_TO_TICKS(200));
            }          

            if (xQueuePeek(uploadQueue, uploadFilename, 0) == pdTRUE && ftpclient.isConnected()) {

                Serial.printf("Uploading: %s\n", uploadFilename);
                snprintf(tempFilename, sizeof(tempFilename), "/%s.temp", uploadFilename);
                bool uploadSuccess = false;

                if (xSemaphoreTake(sdCardMutex, portMAX_DELAY) == pdTRUE) {
                    File fileToUpload = SD.open(uploadFilename);

                    if (fileToUpload) {
                        uint32_t fileSize = fileToUpload.size();
                        xSemaphoreGive(sdCardMutex);
                        
                        currentBlinkState = BLINK_FAST;  // Aktiver Upload
                        Serial.printf("Datei zum Upload: %s, Größe: %u kB\n", uploadFilename, fileSize/1000);                  

                        ftpclient.InitFile("Type I");
                        ftpclient.NewFile(tempFilename);
                        
                        uint32_t bytesUploaded = 0;
                        uint8_t buffer[FTP_BUFFER_SIZE];             

                        vTaskDelay(pdMS_TO_TICKS(50));

                        while (bytesUploaded < fileSize) {
                            
                            if (xSemaphoreTake(sdCardMutex, portMAX_DELAY) == pdTRUE) {
                                size_t bytesRead = fileToUpload.read(buffer, sizeof(buffer));
                                xSemaphoreGive(sdCardMutex);  
                                ftpclient.WriteData(buffer, bytesRead);                       
                                bytesUploaded += bytesRead;           
                            }

                            if(!ftpclient.isConnected()){
                                Serial.println("FTP Verbindung verloren. Upload abgebrochen.");
                                break;
                            }

                            //vTaskDelay(pdMS_TO_TICKS(1));
                            taskYIELD();
                        }
                        
                        ftpclient.CloseFile();
                        fileToUpload.close();
                        
                        if(ftpclient.isConnected() && bytesUploaded == fileSize) {
                            uploadSuccess = true;
                            Serial.printf("Upload von %s abgeschlossen.\n", uploadFilename);
                            ftpclient.RenameFile(tempFilename, uploadFilename);
                            xQueueReceive(uploadQueue, uploadFilename, 0);
                        }

                    }else{
                        xSemaphoreGive(sdCardMutex);
                        Serial.printf("Fehler beim Öffnen der Datei für Upload: %s\n", uploadFilename);                    
                    }
                }
                
                if(!uploadSuccess) {
                    currentBlinkState = BLINK_SLOW;  // Zurück zu langsam bei Fehler
                    vTaskDelay(pdMS_TO_TICKS(FTP_TIMEOUT));
                    Serial.printf("Upload fehlgeschlagen. Datei %s wird erneut in die Warteschlange gestellt.\n", uploadFilename);
                }

                if(uxQueueMessagesWaiting(uploadQueue) == 0) {
                    currentBlinkState = BLINK_NONE;  // Alles fertig
                    if(KoKriRec_State == State_KOKRI_SCHALE_UPLOADING) {
                        ftpclient.InitFile("Type I");
                        ftpclient.NewFile(config.deviceName);
                        ftpclient.CloseFile();
                        KoKriRec_State = State_KOKRI_SCHALE_IDLE;
                        Serial.println("Upload abgeschlossen. finish.txt erstellt.");
                    }
                    Serial.println("Keine weiteren Uploads in der Warteschlange.");
                }
            }
        } else {
            // No WiFi connection, wait a bit before checking again
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
        
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void WiFiControlTask(void* parameter) {
    // Set WiFi to station mode
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();  // Ensure we're not connected
    vTaskDelay(pdMS_TO_TICKS(100));

    while (true) {
        // If we're connected, check if we're still connected
        if (WiFi.status() == WL_CONNECTED) {
            //Serial.println("Still connected to WiFi");
        } else {
            // If we're not connected, scan for networks
            scanNetworks();
        }
        
        vTaskDelay(pdMS_TO_TICKS(5000));  // Wait 5 seconds before next scan/check
    }
    
    vTaskDelete(NULL);
}

#endif // FTP_H