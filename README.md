# Logik Multiplayer

This repository contains the Logik Multiplayer game source code intended to run on ATMEGA328P.


## Compilation & Upload

avr-gcc -mmcu=atmega328p -DF_CPU=16000000UL -Os -o blink.elf blink.c

avr-objcopy -O ihex blink.elf blink.hex

avrdude -c usbasp -p m328p -U flash:w:blink.hex
