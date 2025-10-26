#define F_CPU 16000000UL
#include <util/delay.h>
#include <avr/io.h>
#include "light_ws2812.h"

#define NUM_LEDS 104
#define COLOR_COUNT 6    // selectable colors, black excluded

enum Color {
    COLOR_BLACK = 0,
    COLOR_RED,
    COLOR_GREEN,
    COLOR_BLUE,
    COLOR_YELLOW,
    COLOR_CYAN,
    COLOR_MAGENTA,
};

static const struct cRGB palette[COLOR_COUNT+1] = {
    {0, 0, 0},      // black
    {80, 0, 0},     // red
    {0, 80, 0},     // green
    {0, 0, 80},     // blue
    {40, 40, 0},    // yellow
    {0, 40, 40},    // cyan
    {40, 0, 40},    // magenta
};

struct cRGB led[NUM_LEDS];
uint8_t led_color_codes[NUM_LEDS];

uint8_t player_1_led_position;
uint8_t player_1_led_color;

uint8_t player_2_led_position;
uint8_t player_2_led_color;

static inline void init_adc(void) {
    ADMUX |= (1 << REFS0) | (1 << ADLAR);                   // reference voltage on AVCC, ADC reads only 8 bits
    ADCSRA  = (1 << ADEN)                                   // enable ADC
            | (1 << ADPS2) | (1 << ADPS1) | (1 << ADPS0);   // ADC clock prescaler /128 -> 125 kHz @16 MHz
    DIDR0 = 0x3F;                                           // Disable digital input on all ADC pins (ADC0–ADC5)
}

static inline uint8_t read_adc_channel(uint8_t channel) {
    ADMUX = (ADMUX & 0xF0) | (channel & 0x0F);  // select channel
    ADCSRA |= (1 << ADSC);                      // start ADC conversion
    loop_until_bit_is_clear(ADCSRA, ADSC);      // wait until done
    return ADCH;                                // reads ADC's most significant 8 bits
}

// v: 0..255  ->  returns 0..(n-1)
static inline uint8_t bucket_floor(uint8_t v, uint8_t n) {
    return ((uint16_t)v * n) >> 8;   // == floor(v * n / 256)
}

static inline void update_player_selections(void) {
    player_1_led_position = bucket_floor(read_adc_channel(2), 4);
    player_1_led_color = bucket_floor(read_adc_channel(3), COLOR_COUNT) + 1;
    led_color_codes[player_1_led_position] = player_1_led_color;
    
    player_2_led_position = bucket_floor(read_adc_channel(4), 4) + 100;
    player_2_led_color = bucket_floor(read_adc_channel(5), COLOR_COUNT) + 1;
    led_color_codes[player_2_led_position] = player_2_led_color;
}

static inline void update_color_codes(void) {
    for (uint8_t i = 0; i < NUM_LEDS; i++) {
        led[i] = palette[led_color_codes[i]];
    }
}

int main(void) {
    DDRB |= (1 << DDB0);    // DI for LED strip
    DDRD &= ~((1 << PD6) | (1 << PD1)); // PD6, PD1 behave as inputs
    PORTD |= (1 << PD6) | (1 << PD1);   // pull-up resistors

    // Initialize all LEDs to black
    for (uint8_t i = 0; i < NUM_LEDS; i++) {
        led_color_codes[i] = COLOR_BLACK;
    }

    init_adc();

    while(1) {
        update_player_selections();

        ws2812_setleds(led, NUM_LEDS);

        update_color_codes();

        _delay_ms(50);
    }
}