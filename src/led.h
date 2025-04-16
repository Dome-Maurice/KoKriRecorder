#ifndef LED_H
#define LED_H

#include <Arduino.h>
#include <FastLED.h>
#include "config.h"

extern CRGB leds[NUM_LEDS];

// Static variables for LED audio visualization
static uint8_t currentBrightness = 64;
static uint8_t targetBrightness = 64;
static float decayFactor = 0.8;

void initLED() {
  FastLED.addLeds<LED_TYPE, LED_PIN, COLOR_ORDER>(leds, NUM_LEDS)
    .setCorrection(TypicalLEDStrip)
    .setDither(false);
  
  FastLED.setBrightness(64);
  
  fill_solid(leds, NUM_LEDS, CRGB::Black);
  FastLED.show();
}

void setLEDStatus(CRGB color) {
  leds[0] = color;
  FastLED.show();
}

void idle_Animation(CRGB color, int speed) {
  if (millis() %  speed) {
    for (int i = 0; i < 128; i++) {
      leds[0] = color;
      leds[0].fadeToBlackBy(128 - i);
      FastLED.show();
      vTaskDelay(pdMS_TO_TICKS( (int)((float)speed/2)/128) );
    }
    
    for (int i = 128; i >= 0; i--) {
      leds[0] = color;
      leds[0].fadeToBlackBy(128 - i);
      FastLED.show();
      vTaskDelay(pdMS_TO_TICKS( (int)((float)speed/2)/128) );
    }
  }
}
void updateLEDFromAudio(int32_t sum, int32_t peak, int numSamples) {
    if (numSamples <= 0) return;
    
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
    
    leds[0] = COLOR_RECORDING;
    
    if (currentBrightness > 180) {
        leds[0].g = map(currentBrightness, 180, 255, 0, 70);
    }
    
    FastLED.setBrightness(currentBrightness);
    FastLED.show();
}

#endif // LED_H