#include <util/delay.h>
#include <avr/io.h>
#include <avr/interrupt.h>
#include "light_ws2812.h"

struct cRGB led[4];

int main(void) {
    DDRB |= (1 << DDB0)

    led[1].r=80;led[2].g=80;led[3].b=80;    // Write red to array

    while(1) {
        led[0].r=120;led[0].g=00;led[0].b=0;    // Write red to array
        ws2812_setleds(led,1);
        _delay_ms(500);                         // wait for 500ms.

        led[0].r=0;led[0].g=120;led[0].b=0;			// green
        ws2812_setleds(led,1);
        _delay_ms(500);

        led[0].r=0;led[0].g=00;led[0].b=120;		// blue
        ws2812_setleds(led,1);
        _delay_ms(500);
    }
}