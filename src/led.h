#ifndef LED_H
#define LED_H

#include <Arduino.h>
#include <FastLED.h>
#include <algorithm>
#include "config.h"

extern CRGB statusled[1];
extern CRGB effektleds[EFFEKT_LED_NUM];

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

void idle_Animation(CRGB color, int speed) {
  if (millis() %  speed) {
    for (int i = 0; i < 128; i++) {
      statusled[0] = color;
      statusled[0].fadeToBlackBy(128 - i);
      FastLED.show();
      vTaskDelay(pdMS_TO_TICKS( (int)((float)speed/2)/128) );
    }
    
    for (int i = 128; i >= 0; i--) {
      statusled[0] = color;
      statusled[0].fadeToBlackBy(128 - i);
      FastLED.show();
      vTaskDelay(pdMS_TO_TICKS( (int)((float)speed/2)/128) );
    }
  }
}
void updateLEDFromAudio(int32_t sum, int32_t peak, int numSamples) {
    if (numSamples <= 0) return;

    static byte offset = 0;
    static unsigned long lastUpdate = 0;
    if (millis() - lastUpdate > 100) { 
      offset = (offset + 1) % EFFEKT_LED_NUM;
      lastUpdate = millis();
    }

    // Berechne Durchschnitt und skaliere ihn
    int32_t average = sum / numSamples;
    
    // Kombiniere Durchschnitt und Peak für dynamischeres Verhalten
    int32_t combinedLevel = average * 0.3 + peak * 0.9;
    
    // Skaliere auf 64-255 für Helligkeit
    targetBrightness = constrain(64 + (combinedLevel / 80), 64, 255);
    
    // Nachhall-Effekt
    if (targetBrightness > currentBrightness) {
        currentBrightness = targetBrightness;
    } else {
        currentBrightness = currentBrightness * decayFactor + targetBrightness * (1 - decayFactor);
    }
    
    for(int i = 0; i < EFFEKT_LED_NUM; i++) {
        effektleds[i] = CRGB::Black;
    }

    effektleds[offset] = COLOR_RECORDING;
    effektleds[offset].nscale8(currentBrightness);
    if (currentBrightness > 180) {
      effektleds[offset].g = map(currentBrightness, 180, 255, 0, 70);
    }
    
    
    FastLED.setBrightness(currentBrightness);
    FastLED.show();
}

void sinelon(byte gHue)
{
  // a colored dot sweeping back and forth, with fading trails
  fadeToBlackBy( effektleds, EFFEKT_LED_NUM, 20);
  int pos = beatsin16( 13, 0, EFFEKT_LED_NUM-1 );
  effektleds[pos] += CHSV( gHue, 255, 192);
  
}

// Neue Farbdefinitionen (HSV statt RGB)
#define BASE_HUE 270        // Violett Grundton
#define BASE_SAT 240        // Hohe Sättigung
#define BASE_VAL 40         // Dunklerer Grundwert
#define HIGHLIGHT_HUE 280   // Helleres Violett
#define HIGHLIGHT_SAT 255   // Maximale Sättigung
#define HIGHLIGHT_VAL 200   // Heller Spitzenwert

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
    float base_increment = 0.04;
    float noise_variation = sin(millis() / 2000.0) * noise_factor;
    
    position += base_increment + noise_variation;
    if (position >= EFFEKT_LED_NUM) position = 0;
    
    secondary_position -= (base_increment * 0.7) + noise_variation;
    if (secondary_position < 0) secondary_position = EFFEKT_LED_NUM;
    
    breathe_position += 0.02;
    if (breathe_position > TWO_PI) breathe_position = 0;
    float breathe_factor = 0.85 + 0.15 * sin(breathe_position);
    
    float audio_reactivity = 30.0;
    float speed_factor = 1.0 + (audio_level * audio_reactivity / 1000.0);
    position += base_increment * speed_factor * 0.5;
    
    for (int i = 0; i < EFFEKT_LED_NUM; i++) {
      // Primäre und sekundäre Wellenberechnung
      float distance1 = abs(i - position);
      if (distance1 > EFFEKT_LED_NUM / 2) distance1 = EFFEKT_LED_NUM - distance1;
      
      float distance2 = abs(i - secondary_position);
      if (distance2 > EFFEKT_LED_NUM / 2) distance2 = EFFEKT_LED_NUM - distance2;
      
      float local_wave_width = wave_width * (1.0 + 0.1 * sin(i * 0.5 + millis() / 5000.0));
      
      float wave_value1 = std::max(0.0f, 1.0f - (distance1 / local_wave_width));
      float wave_value2 = std::max(0.0f, (float)(1.0f - (distance2 / (local_wave_width * 1.2)))) * 0.6f;
      
      float combined_wave = std::min(1.0f, wave_value1 + wave_value2);
      combined_wave *= 1.0 + 0.1 * sin(i + millis() / 3000.0);
      
      // Audio-Reaktivität verstärkt
      float audio_factor = 1.0 + (audio_level * audio_reactivity / 300.0); // Verstärkt von 500 auf 300
      combined_wave = std::min(1.0f, combined_wave * audio_factor);
      combined_wave *= breathe_factor;
      
      // HSV Farbinterpolation
      uint8_t hue = map(combined_wave * 255, 0, 255, BASE_HUE, HIGHLIGHT_HUE);
      uint8_t sat = map(combined_wave * 255, 0, 255, BASE_SAT, HIGHLIGHT_SAT);
      uint8_t val = map(combined_wave * 255, 0, 255, BASE_VAL, HIGHLIGHT_VAL);
      
      // Kontrastverstärkung durch nicht-lineare Helligkeitskurve
      val = (uint8_t)(pow(val / 255.0, 0.8) * 255);
      
      effektleds[i] = CHSV(hue, sat, val);
    }
    
    lastUpdate = currentTime;
    FastLED.show();
  }
}

#endif // LED_H