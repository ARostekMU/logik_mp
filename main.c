#define F_CPU 16000000UL
#include <util/delay.h>
#include <avr/io.h>
#include "light_ws2812.h"

#define NUM_LEDS 104
#define COLOR_COUNT 6
#define N_PLAYERS   2
#define N_TURNS     6
#define CODE_LEN    4

typedef struct {
    uint8_t guess[CODE_LEN];
    uint8_t n_pos;
    uint8_t n_col;
    uint8_t committed;
} Turn;

typedef struct {
    Turn turns[N_TURNS];
} Board;

static Board boards[N_PLAYERS];
static uint8_t secret[CODE_LEN];

typedef struct {
    uint8_t guess_led[N_TURNS][CODE_LEN];
    uint8_t eval_led [N_TURNS][CODE_LEN];
} LedMap;

static LedMap ledmap[N_PLAYERS];

typedef enum { GS_PLAYING, GS_P1_WIN, GS_P2_WIN, GS_DRAW } GameState;
static uint8_t current_turn = 0;
static GameState game_state = GS_PLAYING;
static uint8_t draw_winning = 0;

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
    {0, 0, 0}, {15, 0, 0}, {0, 15, 0}, {0, 0, 15},
    {7, 7, 0}, {0, 7, 7}, {7, 0, 7},
};

static const struct cRGB palette_bright[COLOR_COUNT+1] = {
    {0, 0, 0}, {30, 0, 0}, {0, 30, 0}, {0, 0, 30},
    {30, 30, 0}, {0, 30, 30}, {30, 0, 30},
};

struct cRGB led[NUM_LEDS];
uint8_t led_color_codes[NUM_LEDS];

// Cursor and selection
uint8_t player_1_slot, player_1_led_position, player_1_live_color;
uint8_t player_2_slot, player_2_led_position, player_2_live_color;
uint8_t player_1_locked_leds[4];
uint8_t player_2_locked_leds[4];
static uint8_t blink_on = 0;

// Selection LEDs
static const uint8_t select_led[N_PLAYERS][CODE_LEN] = {
    {0, 1, 2, 3},          // Player 1
    {100, 101, 102, 103}   // Player 2
};
static uint8_t p1_sel_color[4];
static uint8_t p2_sel_color[4];

/* -------------------- ADC -------------------- */
static inline void init_adc(void) {
    ADMUX |= (1 << REFS0) | (1 << ADLAR);
    ADCSRA  = (1 << ADEN) | (1 << ADPS2) | (1 << ADPS1) | (1 << ADPS0);
    DIDR0 = 0x3F;
}
static inline uint8_t read_adc_channel(uint8_t channel) {
    ADMUX = (ADMUX & 0xF0) | (channel & 0x0F);
    ADCSRA |= (1 << ADSC);
    loop_until_bit_is_clear(ADCSRA, ADSC);
    return ADCH;
}
static inline uint8_t bucket_floor(uint8_t v, uint8_t n) {
    return ((uint16_t)v * n) >> 8;
}

/* -------------------- Game logic -------------------- */
static void compute_feedback(const uint8_t secret_[CODE_LEN],
                             const uint8_t guess [CODE_LEN],
                             uint8_t *n_pos, uint8_t *n_col)
{
    uint8_t used_s[CODE_LEN] = {0}, used_g[CODE_LEN] = {0};
    uint8_t pos = 0, col = 0;

    for (uint8_t i = 0; i < CODE_LEN; i++) {
        if (guess[i] && guess[i] == secret_[i]) {
            used_s[i] = used_g[i] = 1;
            pos++;
        }
    }

    for (uint8_t i = 0; i < CODE_LEN; i++) {
        if (used_g[i] || !guess[i]) continue;
        for (uint8_t j = 0; j < CODE_LEN; j++) {
            if (used_s[j]) continue;
            if (guess[i] == secret_[j]) {
                used_s[j] = 1;
                col++;
                break;
            }
        }
    }

    *n_pos = pos;
    *n_col = col;
}

static inline void init_board_state(void) {
    for (uint8_t p = 0; p < N_PLAYERS; p++) {
        for (uint8_t t = 0; t < N_TURNS; t++) {
            boards[p].turns[t].n_pos = boards[p].turns[t].n_col = 0;
            boards[p].turns[t].committed = 0;
            for (uint8_t i = 0; i < CODE_LEN; i++) boards[p].turns[t].guess[i] = 0;
        }
    }

    secret[0] = 1; secret[1] = 2; secret[2] = 3; secret[3] = 4;

    for (uint8_t i = 0; i < 4; i++) {
        player_1_locked_leds[i] = 0;
        player_2_locked_leds[i] = 0;
        p1_sel_color[i] = COLOR_BLACK;
        p2_sel_color[i] = COLOR_BLACK;
    }
    current_turn = 0;
    game_state = GS_PLAYING;
    draw_winning = 0;
}

static inline void update_player_selections(void) {
    player_1_slot = bucket_floor(read_adc_channel(2), 4);
    player_2_slot = bucket_floor(read_adc_channel(4), 4);
    player_1_led_position = ledmap[0].guess_led[current_turn][player_1_slot];
    player_2_led_position = ledmap[1].guess_led[current_turn][player_2_slot];
    player_1_live_color = bucket_floor(read_adc_channel(3), COLOR_COUNT) + 1;
    player_2_live_color = bucket_floor(read_adc_channel(5), COLOR_COUNT) + 1;
}

static inline uint8_t both_players_locked_row(void) {
    uint8_t all_locked = 1;
    for (uint8_t i = 0; i < 4; i++) all_locked &= player_1_locked_leds[i];
    for (uint8_t i = 0; i < 4; i++) all_locked &= player_2_locked_leds[i];
    return all_locked;
}

static void commit_and_score_turn(void) {
    for (uint8_t col = 0; col < 4; col++) {
        boards[0].turns[current_turn].guess[col] = p1_sel_color[col];
        boards[1].turns[current_turn].guess[col] = p2_sel_color[col];
    }

    for (uint8_t col = 0; col < 4; col++) {
        uint8_t idx0 = ledmap[0].guess_led[current_turn][col];
        uint8_t idx1 = ledmap[1].guess_led[current_turn][col];
        led_color_codes[idx0] = boards[0].turns[current_turn].guess[col];
        led_color_codes[idx1] = boards[1].turns[current_turn].guess[col];
    }

    compute_feedback(secret, boards[0].turns[current_turn].guess,
                     &boards[0].turns[current_turn].n_pos, &boards[0].turns[current_turn].n_col);
    compute_feedback(secret, boards[1].turns[current_turn].guess,
                     &boards[1].turns[current_turn].n_pos, &boards[1].turns[current_turn].n_col);

    boards[0].turns[current_turn].committed = 1;
    boards[1].turns[current_turn].committed = 1;

    uint8_t p0_win = (boards[0].turns[current_turn].n_pos == CODE_LEN);
    uint8_t p1_win = (boards[1].turns[current_turn].n_pos == CODE_LEN);

    if (p0_win && p1_win) {
        game_state = GS_DRAW; draw_winning = 1;
    } else if (p0_win) {
        game_state = GS_P1_WIN;
    } else if (p1_win) {
        game_state = GS_P2_WIN;
    } else if (current_turn == (N_TURNS - 1)) {
        game_state = GS_DRAW;
    } else {
        game_state = GS_PLAYING;
    }
}

static inline void render_evaluations(void) {
    for (uint8_t p = 0; p < N_PLAYERS; p++) {
        for (uint8_t row = 0; row <= current_turn; row++) {
            if (!boards[p].turns[row].committed) continue;
            uint8_t n_pos = boards[p].turns[row].n_pos;
            uint8_t n_col = boards[p].turns[row].n_col;
            uint8_t peg = 0;
            for (; peg < n_pos && peg < CODE_LEN; peg++) {
                uint8_t idx = ledmap[p].eval_led[row][peg];
                led[idx] = palette_bright[COLOR_RED];
            }
            for (; peg < (n_pos + n_col) && peg < CODE_LEN; peg++) {
                uint8_t idx = ledmap[p].eval_led[row][peg];
                led[idx] = palette_bright[COLOR_YELLOW];
            }
            for (; peg < CODE_LEN; peg++) {
                uint8_t idx = ledmap[p].eval_led[row][peg];
                led[idx] = palette[COLOR_BLACK];
            }
        }
    }
}

/* LED mapping */
static inline void init_ledmap(void) {
    for (uint8_t r = 0; r < 6; r++) {
        uint8_t base = 4 + (uint8_t)(16 * r);
        ledmap[0].guess_led[r][0] = base + 2;
        ledmap[0].guess_led[r][1] = base + 3;
        ledmap[0].guess_led[r][2] = base + 4;
        ledmap[0].guess_led[r][3] = base + 5;
        ledmap[0].eval_led[r][0]  = base + 0;
        ledmap[0].eval_led[r][1]  = base + 1;
        ledmap[0].eval_led[r][2]  = base + 14;
        ledmap[0].eval_led[r][3]  = base + 15;
    }

    for (uint8_t r = 0; r < 6; r++) {
        uint8_t base = 97 - (uint8_t)(16 * r);
        ledmap[1].guess_led[r][0] = base - 4;
        ledmap[1].guess_led[r][1] = base - 5;
        ledmap[1].guess_led[r][2] = base - 6;
        ledmap[1].guess_led[r][3] = base - 7;
        ledmap[1].eval_led[r][0]  = base - 0;
        ledmap[1].eval_led[r][1]  = base - 1;
        ledmap[1].eval_led[r][2]  = base - 2;
        ledmap[1].eval_led[r][3]  = base - 3;
    }
}

/* -------------------- Main -------------------- */
int main(void) {
    DDRB |= (1 << DDB0);
    DDRD &= ~((1 << PD6) | (1 << PD1));
    PORTD |= (1 << PD6) | (1 << PD1);

    for (uint8_t i = 0; i < NUM_LEDS; i++) led_color_codes[i] = COLOR_BLACK;

    init_ledmap();
    init_adc();
    init_board_state();

    while (1) {
        update_player_selections();
        uint8_t p1_pressed = !(PIND & (1 << PD6));
        uint8_t p2_pressed = !(PIND & (1 << PD1));

        if (p1_pressed) {
            player_1_locked_leds[player_1_slot] = 1;
            p1_sel_color[player_1_slot] = player_1_live_color;
        }
        if (p2_pressed) {
            player_2_locked_leds[player_2_slot] = 1;
            p2_sel_color[player_2_slot] = player_2_live_color;
        }

        if (both_players_locked_row()) {
            _delay_ms(50);
            while (!(PIND & (1 << PD6)) || !(PIND & (1 << PD1))) { _delay_ms(10); }

            commit_and_score_turn();
            if (game_state == GS_PLAYING) {
                current_turn++;
                for (uint8_t i = 0; i < 4; i++) {
                    player_1_locked_leds[i] = 0;
                    player_2_locked_leds[i] = 0;
                    p1_sel_color[i] = COLOR_BLACK;
                    p2_sel_color[i] = COLOR_BLACK;
                }
            }
        }

        /* ------- Drawing ------- */
        for (uint8_t i = 0; i < NUM_LEDS; i++) led[i] = palette[led_color_codes[i]];
        render_evaluations();

        if (game_state == GS_PLAYING) {
            for (uint8_t c = 0; c < 4; c++) {
                led[ select_led[0][c] ] = palette[COLOR_BLACK];
                led[ select_led[1][c] ] = palette[COLOR_BLACK];
            }

            for (uint8_t c = 0; c < 4; c++) {
                if (player_1_locked_leds[c])
                    led[ select_led[0][c] ] = palette_bright[ p1_sel_color[c] ];
            }
            if (!player_1_locked_leds[player_1_slot])
                led[ select_led[0][player_1_slot] ] = blink_on ? palette_bright[player_1_live_color]
                                                               : palette[COLOR_BLACK];

            for (uint8_t c = 0; c < 4; c++) {
                if (player_2_locked_leds[c])
                    led[ select_led[1][c] ] = palette_bright[ p2_sel_color[c] ];
            }
            if (!player_2_locked_leds[player_2_slot])
                led[ select_led[1][player_2_slot] ] = blink_on ? palette_bright[player_2_live_color]
                                                               : palette[COLOR_BLACK];
        } else {
            uint8_t blink_p0 = (game_state == GS_P1_WIN) || (game_state == GS_DRAW && draw_winning);
            uint8_t blink_p1 = (game_state == GS_P2_WIN) || (game_state == GS_DRAW && draw_winning);

            if (blink_p0) {
                for (uint8_t c = 0; c < 4; c++) {
                    uint8_t idx = select_led[0][c];
                    uint8_t col = p1_sel_color[c];
                    led[idx] = blink_on ? palette_bright[col] : palette[COLOR_BLACK];
                }
            }
            if (blink_p1) {
                for (uint8_t c = 0; c < 4; c++) {
                    uint8_t idx = select_led[1][c];
                    uint8_t col = p2_sel_color[c];
                    led[idx] = blink_on ? palette_bright[col] : palette[COLOR_BLACK];
                }
            }
        }

        ws2812_setleds(led, NUM_LEDS);

        _delay_ms(50);
        static uint8_t frame_counter = 0;
        frame_counter = (frame_counter + 1) % 20;
        blink_on = (frame_counter >= 4);
    }
}
