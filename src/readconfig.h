#include "config.h"

// Globale Konfigurationsstruktur
RecorderConfig config;

extern SemaphoreHandle_t sdCardMutex;

// Funktion zum Lesen der Konfigurationsdatei
bool loadConfigFromSD() {
    Serial.println("Lade Konfiguration von SD-Karte...");
    
    // Standardwerte setzen
    strcpy(config.deviceName, "AudioRecorder");
    strcpy(config.wifiSSID, "");
    strcpy(config.wifiPassword, "");
    strcpy(config.ftpServer, "");
    strcpy(config.ftpUser, "");
    strcpy(config.ftpPassword, "");
    config.ftpPort = 21;
    config.ftpEnabled = false;
    config.webserverEnabled = false;
    config.audioGain = 0.5f;  // Standardwert für audioGain
    
    // Prüfen, ob SD-Karte bereit ist
    if (xSemaphoreTake(sdCardMutex, portMAX_DELAY) != pdTRUE) {
        Serial.println("Konnte SD-Karten-Mutex nicht erhalten");
        return false;
    }
    
    // Prüfen, ob Konfigurationsdatei existiert
    if (!SD.exists(CONFIG_FILENAME)) {
        Serial.printf("Konfigurationsdatei %s nicht gefunden, verwende Standardwerte\n", CONFIG_FILENAME);
        
        // Erstelle eine Beispiel-Konfigurationsdatei
        File configFile = SD.open(CONFIG_FILENAME, FILE_WRITE);
        if (configFile) {
            configFile.println("# Konfigurationsdatei für Audio Recorder");
            configFile.println("# Format: schlüssel=wert");
            configFile.println("");
            configFile.println("# Gerätename");
            configFile.println("deviceName=AudioRecorder");
            configFile.println("");
            configFile.println("# WLAN Konfiguration");
            configFile.println("wifiSSID=MeinWLAN");
            configFile.println("wifiPassword=MeinPasswort");
            configFile.println("");
            configFile.println("# FTP Konfiguration");
            configFile.println("ftpEnabled=false");
            configFile.println("ftpServer=ftp.example.com");
            configFile.println("ftpUser=username");
            configFile.println("ftpPassword=password");
            configFile.println("ftpPort=21");
            configFile.println("# Webserver Konfiguration");
            configFile.println("webServerEnabled=false");
            configFile.println("# Audio Konfiguration");
            configFile.println("audioGain=0.5");
            configFile.close();
            Serial.println("Beispiel-Konfigurationsdatei erstellt");
        } else {
            Serial.println("Fehler beim Erstellen der Beispiel-Konfigurationsdatei");
        }
        
        xSemaphoreGive(sdCardMutex);
        return false;
    }
    
    // Konfigurationsdatei öffnen und lesen
    File configFile = SD.open(CONFIG_FILENAME, FILE_READ);
    if (!configFile) {
        Serial.printf("Konnte Konfigurationsdatei %s nicht öffnen\n", CONFIG_FILENAME);
        xSemaphoreGive(sdCardMutex);
        return false;
    }
    
    // Puffer für die aktuelle Zeile
    char line[128];
    int lineIndex = 0;
    
    // Datei zeilenweise lesen
    while (configFile.available()) {
        char c = configFile.read();
        
        // Zeilenende erkennen
        if (c == '\n' || c == '\r' || !configFile.available()) {
            // Zeile abschließen
            if (lineIndex > 0) {
                line[lineIndex] = '\0';
                
                // Kommentarzeilen und leere Zeilen überspringen
                if (line[0] != '#' && line[0] != '\0') {
                    // Nach Schlüssel=Wert-Paaren suchen
                    char* separator = strchr(line, '=');
                    if (separator != NULL) {
                        // Schlüssel und Wert extrahieren
                        *separator = '\0';
                        char* key = line;
                        char* value = separator + 1;
                        
                        // Leerzeichen am Ende des Schlüssels entfernen
                        char* end = key + strlen(key) - 1;
                        while (end > key && isspace(*end)) {
                            *end = '\0';
                            end--;
                        }
                        
                        // Leerzeichen am Anfang des Wertes entfernen
                        while (isspace(*value)) {
                            value++;
                        }
                        
                        // Wert in Konfigurationsstruktur speichern
                        if (strcmp(key, "deviceName") == 0) {
                            strncpy(config.deviceName, value, MAX_VALUE_LEN - 1);
                        } else if (strcmp(key, "wifiSSID") == 0) {
                            strncpy(config.wifiSSID, value, MAX_VALUE_LEN - 1);
                        } else if (strcmp(key, "wifiPassword") == 0) {
                            strncpy(config.wifiPassword, value, MAX_VALUE_LEN - 1);
                        } else if (strcmp(key, "ftpServer") == 0) {
                            strncpy(config.ftpServer, value, MAX_VALUE_LEN - 1);
                        } else if (strcmp(key, "ftpUser") == 0) {
                            strncpy(config.ftpUser, value, MAX_VALUE_LEN - 1);
                        } else if (strcmp(key, "ftpPassword") == 0) {
                            strncpy(config.ftpPassword, value, MAX_VALUE_LEN - 1);
                        } else if (strcmp(key, "ftpPort") == 0) {
                            config.ftpPort = atoi(value);
                        } else if (strcmp(key, "ftpEnabled") == 0) {
                            // Boolesche Werte verarbeiten
                            if (strcmp(value, "true") == 0 || strcmp(value, "1") == 0) {
                                config.ftpEnabled = true;
                            } else {
                                config.ftpEnabled = false;
                            }
                        } else if (strcmp(key, "webServerEnabled") == 0) {
                            // Boolesche Werte verarbeiten
                            if (strcmp(value, "true") == 0 || strcmp(value, "1") == 0) {
                                config.webserverEnabled = true;
                            } else {
                                config.webserverEnabled = false;
                            }
                        } else if (strcmp(key, "audioGain") == 0) {
                            config.audioGain = atof(value);
                        }
                    }
                }
                
                // Reset für die nächste Zeile
                lineIndex = 0;
            }
        } else if (lineIndex < sizeof(line) - 1) {
            // Zeichen zur aktuellen Zeile hinzufügen
            line[lineIndex++] = c;
        }
    }
    
    // Datei schließen und Mutex freigeben
    configFile.close();
    xSemaphoreGive(sdCardMutex);
    
    // Konfiguration ausgeben
    Serial.println("Konfiguration geladen:");
    Serial.printf("  Gerätename: %s\n", config.deviceName);
    Serial.printf("  WLAN SSID: %s\n", config.wifiSSID);
    Serial.printf("  WLAN Passwort: %s\n", config.wifiPassword);
    Serial.printf("  FTP aktiviert: %s\n", config.ftpEnabled ? "Ja" : "Nein");
    Serial.printf("  FTP Server: %s\n", config.ftpServer);
    Serial.printf("  FTP Benutzer: %s\n", config.ftpUser);
    Serial.printf("  FTP Passwort: %s\n", config.ftpPassword);
    Serial.printf("  FTP Port: %u\n", config.ftpPort);
    Serial.printf("  Webserver aktiviert: %s\n", config.webserverEnabled ? "Ja" : "Nein");
    Serial.printf("  Audio Gain: %.2f\n", config.audioGain);
    
    return true;
}