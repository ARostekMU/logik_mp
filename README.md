# Logik Multiplayer

This repository contains the Logik Multiplayer game source code intended to run on ATMEGA328P.


## Compilation & Upload

### MacOS

avr-gcc -mmcu=atmega328p -DF_CPU=16000000UL -Os main.c ws2812/light_ws2812.c -c main.elf -Iws2812
avr-objcopy -O ihex -R .eeprom main.elf main.hex
avrdude -c usbasp -p m328p -U flash:w:main.hex
