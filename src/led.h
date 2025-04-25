#ifndef LED_H
#define LED_H

#include <Arduino.h>
#include <FastLED.h>
#include <algorithm>
#include "config.h"

extern CRGB statusled[1];
extern CRGB effektleds[EFFEKT_LED_NUM];
extern volatile int peakAudioLevel;

// Static variables for LED audio visualization
static uint8_t currentBrightness = 64;
static uint8_t targetBrightness = 64;
static float decayFactor = 0.8;

void initLED() {
  FastLED.addLeds<LED_TYPE, STATUS_LED_PIN, COLOR_ORDER>(statusled, 1);
  FastLED.addLeds<LED_TYPE, EFFEKT_LED_PIN, COLOR_ORDER>(effektleds, EFFEKT_LED_NUM);

  FastLED.setBrightness(200);
  
  fill_solid(statusled, 1, CRGB::Black);
  fill_solid(effektleds, EFFEKT_LED_NUM, CRGB::Black);
  FastLED.show();
}

void setLEDStatus(CRGB color) {
  statusled[0] = color;
  FastLED.show();
}


// Verbesserte HSV-Interpolation für sanfte Farbübergänge
CHSV interpolateHSV(CHSV start, CHSV target, float factor) {
    factor = constrain(factor, 0.0f, 1.0f);
    
    // Berechne kürzesten Weg für Hue-Übergang
    int16_t hueDiff = (int16_t)target.hue - (int16_t)start.hue;
    if (hueDiff > 127) hueDiff -= 256;
    if (hueDiff < -128) hueDiff += 256;
    
    uint8_t hue = start.hue + (hueDiff * factor);
    uint8_t sat = start.sat + ((target.sat - start.sat) * factor);
    uint8_t val = start.val + ((target.val - start.val) * factor);
    
    return CHSV(hue, sat, val);
}

// Initialisiere die Farbvariablen mit sinnvollen Startwerten
static CHSV currentBaseColor(96, 240, 40);      // Grün als Standardfarbe
static CHSV targetBaseColor(96, 240, 40);
static CHSV currentHighlightColor(96, 255, 200);
static CHSV targetHighlightColor(96, 255, 200);
static float colorTransitionFactor = 1.0f;      // Start mit abgeschlossenem Übergang

void updateStateColor(uint8_t hue, uint8_t saturation, uint8_t baseValue, uint8_t highlightValue) {
    // Wenn eine neue Farbe gesetzt wird während noch ein Übergang läuft,
    // aktualisieren wir die aktuelle Farbe zum Zwischenstand
    if (colorTransitionFactor < 1.0f) {
        currentBaseColor = interpolateHSV(currentBaseColor, targetBaseColor, colorTransitionFactor);
        currentHighlightColor = interpolateHSV(currentHighlightColor, targetHighlightColor, colorTransitionFactor);
    }
    
    // Setze neue Zielfarben
    targetBaseColor = CHSV(hue, saturation, constrain(baseValue, 0, 255));
    targetHighlightColor = CHSV(hue, saturation, constrain(highlightValue, baseValue, 255));
    
    // Starte neuen Übergang nur wenn sich die Farben tatsächlich ändern
    if (targetBaseColor.hue != currentBaseColor.hue ||
        targetBaseColor.sat != currentBaseColor.sat ||
        targetBaseColor.val != currentBaseColor.val ||
        targetHighlightColor.val != currentHighlightColor.val) {
        colorTransitionFactor = 0.0f;
    }
}


#define IDLE_SPEED  15         // Geschwindigkeit im Idle-Zustand (je höher, desto langsamer)
#define BRIGHTNESS  150        // Helligkeit (0-255)
#define UPDATE_INTERVAL 15

// Animations-Variablen
float position = 0;
float wave_width = 2.5;        // Breite der Hauptwelle
float secondary_position = 6;  // Position der zweiten Welle (versetzt)
float noise_factor = 0.15;     // Natürliche Variation
float breathe_position = 0;    // Für Atembewegung
unsigned long lastUpdate = 0;

void updateAnimation(int audio_level) {
    unsigned long currentTime = millis();
    if (currentTime - lastUpdate >= UPDATE_INTERVAL) {
        // Audio-Level-Normalisierung und verstärkte Reaktivität
        float audioFactor = constrain(audio_level / 255.0f, 0.0f, 1.0f);
        float peakFactor = constrain(peakAudioLevel / 255.0f, 0.0f, 1.0f);
        
        audioFactor = pow(audioFactor, 0.7f);
        peakFactor = pow(peakFactor, 0.6f); // Stärkere nicht-lineare Verstärkung für Peaks
        
        float audioHueShift = audioFactor * 30.0f + peakFactor * 20.0f;
        float audioValBoost = audioFactor * 80.0f + peakFactor * 60.0f;
        float speedBoost = audioFactor * 2.0f + peakFactor * 1.5f;
        
        // Farbübergang aktualisieren
        if (colorTransitionFactor < 1.0f) {
            colorTransitionFactor += 0.02f;
            if (colorTransitionFactor > 1.0f) colorTransitionFactor = 1.0f;
            
            currentBaseColor = interpolateHSV(currentBaseColor, targetBaseColor, colorTransitionFactor);
            currentHighlightColor = interpolateHSV(currentHighlightColor, targetHighlightColor, colorTransitionFactor);
        }

        // Verstärkte Bewegungslogik
        float base_increment = 0.04 * (1.0f + speedBoost);
        float noise_variation = sin(millis() / 2000.0) * (noise_factor + audioFactor * 0.2);
        
        position += base_increment + noise_variation;
        if (position >= EFFEKT_LED_NUM) position = 0;
        
        secondary_position -= (base_increment * 0.7) + noise_variation;
        if (secondary_position < 0) secondary_position = EFFEKT_LED_NUM;
        
        breathe_position += 0.02 * (1.0f + audioFactor);
        if (breathe_position > TWO_PI) breathe_position = 0;
        float breathe_factor = constrain(0.85 + 0.15 * sin(breathe_position) + audioFactor * 0.3, 0.0f, 1.0f);
        
        for (int i = 0; i < EFFEKT_LED_NUM; i++) {
            float distance1 = abs(i - position);
            if (distance1 > EFFEKT_LED_NUM / 2) distance1 = EFFEKT_LED_NUM - distance1;
            
            float distance2 = abs(i - secondary_position);
            if (distance2 > EFFEKT_LED_NUM / 2) distance2 = EFFEKT_LED_NUM - distance2;
            
            float local_wave_width = wave_width * (1.0 + audioFactor * 0.5 + 0.1 * sin(i * 0.5 + millis() / 5000.0));
            
            float wave_value1 = std::max(0.0f, 1.0f - (distance1 / local_wave_width));
            float wave_value2 = std::max(0.0f, (float)(1.0f - (distance2 / (local_wave_width * 1.2)))) * 0.6f;
            
            float combined_wave = std::min(1.0f, wave_value1 + wave_value2 + audioFactor * 0.2f);
            combined_wave = constrain(combined_wave * (1.0 + 0.2 * sin(i + millis() / 3000.0)), 0.0f, 1.0f);
            combined_wave = constrain(combined_wave * breathe_factor, 0.0f, 1.0f);
            
            // Verstärkte Audio-reaktive Farbanpassungen
            CHSV baseColorMod = currentBaseColor;
            CHSV highlightColorMod = currentHighlightColor;
            
            // Verstärkte Hue-Modulation
            float hueOffset = audioHueShift * sin(i * 0.5f + millis() / 1000.0f);
            baseColorMod.hue += hueOffset;
            highlightColorMod.hue += hueOffset;
            
            // Verstärkte Helligkeits-Modulation
            float valOffset = audioValBoost * (0.5f + 0.5f * sin(i * 0.7f + millis() / 800.0f));
            highlightColorMod.val = constrain(highlightColorMod.val + valOffset, 0, 255);
            baseColorMod.val = constrain(baseColorMod.val + valOffset * 0.3f, 0, 255);
            
            // Sättigungs-Modulation hinzugefügt
            uint8_t satBoost = audioFactor * 40;
            highlightColorMod.sat = constrain(highlightColorMod.sat + satBoost, 0, 255);
            
            CHSV pixelColor = interpolateHSV(baseColorMod, highlightColorMod, combined_wave);
            effektleds[i] = pixelColor;
        }
        
        lastUpdate = currentTime;
        FastLED.show();
    }
}

#endif // LED_H