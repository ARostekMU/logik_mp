#define F_CPU 16000000UL
#include <util/delay.h>
#include <avr/io.h>
#include "light_ws2812.h"

#define NUM_LEDS 4

struct cRGB led[4];

static inline void initADC5(void) {
    ADMUX |= (1 << REFS0);                                              /* reference voltage on AVCC */
    ADCSRA = (1 << ADEN) | (1 << ADPS2) | (1 << ADPS1) | (1 << ADPS0);  /* ADC clock prescaler /128 */
    ADCSRA |= (1 << ADEN);                                              /* enable ADC */
}

int main(void) {
    DDRB |= (1 << DDB0);    // DI for LED strip

    initADC5();

    uint8_t ledValue;
    uint16_t adcValue;

    led[1].r=80;led[2].g=80;led[3].b=80;

    while(1) {
        ADCSRA |= (1 << ADSC);                     /* start ADC conversion */
        loop_until_bit_is_clear(ADCSRA, ADSC);          /* wait until done */
        adcValue = ADC;                                     /* read ADC in */
        ledValue = (adcValue >> 2); // from 1024 to 256 range

        led[0].r=ledValue;    // LED intensity red
        ws2812_setleds(led, NUM_LEDS);

        _delay_ms(50);
    }
}