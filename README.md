# KoKriRecorder

## Hardware

## FTP 
Einfacher FTP Server mit pyftpdlib
- python3 -m pyftpdlib -p 2121 -u Kokri -P Kokri -n 10.2.3.248 -d ftptest/ -r 6000-6001 --write
 
## Pin Belegung
### Pin Belegung der Komponenten

- **I2S Mikrofon INMP441**
    - WS (Word Select)  -> GPIO 12
    - SCK (Serial Clock)-> GPIO 13
    - SD (Serial Data)  -> GPIO 11
    - L/R               -> GND
    - VDD               -> 3,3V
    - GND               -> GND

- **SD-Karte**
    - CS (Chip Select)  -> GPIO 4
    - MOSI              -> GPIO 6
    - MISO              -> GPIO 7
    - SCK               -> GPIO 5
    - VCC               -> 3,3V
    - GND               -> GND

- **Button**
    - Signal            -> GPIO 9

- **Ladenschalen Reedkontakt**
    - Signal            -> GPIO 10

- **WS2812 LED RING**
    - VCC             -> 5Vin (IN-OUT Lötbrücke muss geschlossen werden um USB Spannung abzugreifen)
    - Data            -> GPIO 20
    - GND             -> GND
- **WS2812 LED Status Onboard**
    - VCC             -> 5Vin (IN-OUT Lötbrücke muss geschlossen werden um USB Spannung abzugreifen)
    - Data            -> GPIO 48
    - GND             -> GND