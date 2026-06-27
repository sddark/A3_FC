/*
 * A3_FC - blink test
 * Blinks OUT1 (PD5, MCU pin 9) at ~1 Hz to prove the avr-gcc -> avrdude(linuxspi)
 * toolchain end-to-end and that the chip is executing our code.
 * Wire an LED + ~330R from the OUT1 header pin to GND.
 *
 * F_CPU is set in the Makefile. The board uses an external crystal (lfuse=0xf7).
 * If the LED blinks at ~2 Hz the crystal is ~8 MHz; ~1 Hz means ~16 MHz, which
 * also tells us the real crystal frequency.
 */
#include <avr/io.h>
#include <util/delay.h>

#define OUT1 (1 << PD5)

int main(void)
{
    DDRD |= OUT1;             /* OUT1 (PD5) as output */

    for (;;) {
        PORTD ^= OUT1;        /* toggle */
        _delay_ms(500);       /* -> 1 Hz blink */
    }
}
