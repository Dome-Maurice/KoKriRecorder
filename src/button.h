#ifndef BUTTON_H
#define BUTTON_H

#include <Arduino.h>
#include "config.h"
#include "recording.h"

class Button {
private:
    const uint8_t pin;
    const uint32_t debounceDelay;
    uint32_t lastDebounceTime;
    bool lastButtonState;
    bool buttonState;

public:
    Button(uint8_t pin, uint32_t debounceDelay = BUTTON_DEBOUNCE_TIME) 
        : pin(pin), 
          debounceDelay(debounceDelay),
          lastDebounceTime(0),
          lastButtonState(HIGH),
          buttonState(HIGH) {
        pinMode(pin, INPUT_PULLUP);

    }

    bool isPressed() {
        bool reading = digitalRead(pin);

        if (reading == LOW && lastButtonState == HIGH) {
            buttonState = LOW;
        }

        if (reading != lastButtonState) {
            lastDebounceTime = millis();
        }

        if ((millis() - lastDebounceTime) > debounceDelay && reading != buttonState) {
            buttonState = reading;
        }

        lastButtonState = reading;
        return (buttonState == LOW); // LOW means pressed (INPUT_PULLUP)
    }
};


#endif // BUTTON_H