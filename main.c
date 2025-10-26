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

// Dim palette (default look, lower brightness)
static const struct cRGB palette[COLOR_COUNT+1] = {
    {0, 0, 0},      // black
    {15, 0, 0},     // red (dim)
    {0, 15, 0},     // green (dim)
    {0, 0, 15},     // blue (dim)
    {7, 7, 0},    // yellow (dim)
    {0, 7, 7},    // cyan (dim)
    {7, 0, 7},    // magenta (dim)
};

// Bright palette (cursor highlight & locked-under-cursor, ~sum ≈ 40)
static const struct cRGB palette_bright[COLOR_COUNT+1] = {
    {0, 0, 0},      // black
    {30, 0, 0},     // red (bright)
    {0, 30, 0},     // green (bright)
    {0, 0, 30},     // blue (bright)
    {30, 30, 0},    // yellow (bright)
    {0, 30, 30},    // cyan (bright)
    {30, 0, 30},    // magenta (bright)
};

struct cRGB led[NUM_LEDS];
uint8_t led_color_codes[NUM_LEDS];

uint8_t player_1_led_position;
uint8_t player_1_live_color;   // <-- NEW: live color from ADC for P1

uint8_t player_2_led_position;
uint8_t player_2_live_color;   // <-- NEW: live color from ADC for P2

uint8_t player_1_locked_leds[4];
uint8_t player_2_locked_leds[4];

static uint8_t blink_on = 0;

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
    // Positions
    player_1_led_position = bucket_floor(read_adc_channel(2), 4);
    player_2_led_position = bucket_floor(read_adc_channel(4), 4) + 100;

    // Live (preview) colors from ADC (do NOT write to led_color_codes here)
    player_1_live_color = bucket_floor(read_adc_channel(3), COLOR_COUNT) + 1;
    player_2_live_color = bucket_floor(read_adc_channel(5), COLOR_COUNT) + 1;
}

static inline void update_color_codes(void) {
    // Base fill: DIM stored colors everywhere
    for (uint8_t i = 0; i < NUM_LEDS; i++) {
        led[i] = palette[led_color_codes[i]];
    }
}

static inline uint8_t evaluate_turn(void) {
    uint8_t all_locked = 1;
    for (uint8_t i = 0; i < 4; i++) all_locked &= player_1_locked_leds[i];
    for (uint8_t i = 0; i < 4; i++) all_locked &= player_2_locked_leds[i];

    if (all_locked) {
        for (uint8_t i = 0; i < 4; i++) {
            player_1_locked_leds[i] = 0;
            player_2_locked_leds[i] = 0;
        }
        return 1; // turn ended
    }
    return 0;     // still ongoing
}

int main(void) {
    DDRB |= (1 << DDB0);    // DI for LED strip
    DDRD &= ~((1 << PD6) | (1 << PD1)); // PD6, PD1 behave as inputs for buttons
    PORTD |= (1 << PD6) | (1 << PD1);   // pull-up resistors

    // Initialize all LEDs to black
    for (uint8_t i = 0; i < NUM_LEDS; i++) {
        led_color_codes[i] = COLOR_BLACK;
    }

    for (uint8_t i = 0; i < 4; i++) {
        player_1_locked_leds[i] = 0;
        player_2_locked_leds[i] = 0;
    }

    init_adc();

    while(1) {
        update_player_selections();

        // Read buttons (active low: pressed = 0)
        uint8_t p1_pressed = !(PIND & (1 << PD6));
        uint8_t p2_pressed = !(PIND & (1 << PD1));

        // Lock action: commit live color to stored code
        if (p1_pressed) {
            player_1_locked_leds[player_1_led_position] = 1;
            led_color_codes[player_1_led_position] = player_1_live_color;   // commit on lock
        }
        if (p2_pressed) {
            player_2_locked_leds[player_2_led_position - 100] = 1;
            led_color_codes[player_2_led_position] = player_2_live_color;   // commit on lock
        }

        if (evaluate_turn()) {
            _delay_ms(50);  // short debounce

            // wait until both buttons released before starting next turn
            while (!(PIND & (1 << PD6)) || !(PIND & (1 << PD1))) {
                _delay_ms(10);
            }

            // refresh selection
            for (uint8_t i = 0; i < 4; i++) {
                led_color_codes[i] = COLOR_BLACK;          // P1 area
                led_color_codes[100 + i] = COLOR_BLACK;    // P2 area
            }
        }

        update_color_codes(); // base DIM fill from stored codes

        // compute lock flags for the *current cursor positions*
        uint8_t p1_locked_here = player_1_locked_leds[player_1_led_position];
        uint8_t p2_locked_here = player_2_locked_leds[player_2_led_position - 100];

        // Overlay for Player 1 cursor position
        if (p1_locked_here) {
            // locked under cursor: steady BRIGHT stored color
            led[player_1_led_position] = palette_bright[ led_color_codes[player_1_led_position] ];
        } else {
            // not locked: BLINK BRIGHT with live color vs black
            led[player_1_led_position] = blink_on
                ? palette_bright[player_1_live_color]
                : palette[COLOR_BLACK];
        }

        // Overlay for Player 2 cursor position
        if (p2_locked_here) {
            led[player_2_led_position] = palette_bright[ led_color_codes[player_2_led_position] ];
        } else {
            led[player_2_led_position] = blink_on
                ? palette_bright[player_2_live_color]
                : palette[COLOR_BLACK];
        }

        ws2812_setleds(led, NUM_LEDS);

        _delay_ms(50);

        static uint8_t frame_counter = 0;

        frame_counter++;
        if (frame_counter >= 20) frame_counter = 0;   // 0..19
        blink_on = (frame_counter >= 4);  // 0..3 => 0 (off), 4..19 => 1 (on)
    }
}
