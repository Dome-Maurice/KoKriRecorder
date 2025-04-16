#ifndef LED_H
#define LED_H

#include <Arduino.h>
#include <FastLED.h>
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

  FastLED.setBrightness(128);
  
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


#endif // LED_H