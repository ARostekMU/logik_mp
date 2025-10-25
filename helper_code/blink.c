#define F_CPU 16000000UL  // 16 MHz clock frequency
#include <avr/io.h>
#include <util/delay.h>

int main(void)
{
    // Set PB0 as output
    DDRB |= (1 << DDB0);

    while (1)
    {
        // Toggle PB0
        PORTB ^= (1 << PORTB0);

        // Wait 500 ms
        _delay_ms(500);
    }
}
