#ifndef WEBSERVER_H
#define WEBSERVER_H

#include <Arduino.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <SD.h>
#include "config.h"

extern RecorderConfig config;

// Webserver-Instanz
AsyncWebServer server(WEB_SERVER_PORT);

// Hilfsfunktion zum URL-Dekodieren
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


// Webserver einrichten
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

  // Handler zum Löschen von Dateien
  server.on("/delete", HTTP_GET, [](AsyncWebServerRequest *request){
    if (!request->hasParam("file")) {
      request->send(400, "text/plain", "Dateiparameter fehlt.");
      return;
    }
  
    String fileToDelete = request->getParam("file")->value();
  
    // URL-dekodieren (für Sonderzeichen etc.)
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

  // Webserver starten
  server.begin();
  Serial.println("Webserver gestartet.");
}

// Initialisiere den Webserver, wenn aktiviert
void initWebServer() {
    setupWebServer();
}

#endif // WEBSERVER_H