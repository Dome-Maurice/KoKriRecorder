# KoKriRecorder - Audio Recorder Project
# Naja Readme generieren geht erstaunlich schlecht

## Overview
An audio recorder project built with an ESP32-S3 microcontroller that allows recording and managing audio files via SD card storage and provides status feedback through an RGB LED. The system records audio in WAV format and stores it on an SD card.

## Hardware Requirements
- ESP32-S3 Development Board
- SD Card Module (SPI)
- WS2812 RGB LED
- Pushbutton (for recording control)
- INMP441 I2S MEMS Microphone Module

## Complete Pin Configuration
```
SD Card Module (SPI):
- MISO: Pin 13
- MOSI: Pin 11
- SCK: Pin 5
- CS: Pin 10

Microphone (I2S):
- BCLK: Pin 16
- LRCLK/WS: Pin 15
- DATA: Pin 14
- SCK: Connected to BCLK

Control:
- Record Button: Pin 9 (Active LOW with internal pullup)

LED Indicator:
- WS2812 Data: Pin 48
- Number of LEDs: 1
- Color Order: GRB
```

## Hardware Connection Details
- The SD card module operates at 3.3V
- INMP441 microphone requires 3.3V power supply
- WS2812 LED operates at 5V logic level (ESP32-S3 is 5V tolerant)
- Button connects between Pin 9 and GND

## LED Status Indicators
The WS2812 RGB LED provides visual feedback about the system state:
- Green: Ready/Idle
- Red: Recording
- Blue: Processing
- Yellow: Warning (SD card issues)
- White: Error (System/Hardware failure)

## Configuration
The system uses a configuration file (`config.txt`) stored on the SD card to manage settings:
- Sample Rate: 44100 Hz
- Bit Depth: 16 bit
- Maximum recording time: Limited by SD card space
- Maximum filename length: 32 characters
- Maximum configuration value length: 64 characters

## Development Platform
Built using:
- PlatformIO IDE
- Arduino Framework for ESP32
- FastLED Library v3.5.0
- ESP32-Arduino-Audio-Recording Library
- SD Card library

## Setup Instructions
1. Install required libraries via PlatformIO
2. Format SD card as FAT32
3. Connect hardware according to pin configuration
4. Upload firmware
5. Create config.txt on SD card if custom settings needed

## Notes
- Make sure to properly format the SD card before first use
- The temporary recording file will be created automatically
- Check LED status for system feedback
- Recommended SD card size: 8GB or larger
- Use high-quality power supply to avoid noise