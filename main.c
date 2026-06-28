/*
 * A3_FC - MPU-6050 read (working bring-up)
 *
 * Streams WHO_AM_I + accel/gyro over the hardware USART so we can watch values
 * on the Pi.  UART TX = PD1/TXD (the AIL input pin; OK while AIL is unwired on
 * the bench). 57600 8N1 -> read /dev/serial0 @57600 on the Pi.
 *
 * MPU-6050 quirks on this board (hard-won, see memory mpu-i2c-no-ack):
 *   - I2C address is 0x69 (AD0 tied HIGH), NOT 0x68.
 *   - The bus can lock up (MPU holds SDA low). Cure: bit-bang bus-recovery
 *     before init + enable the MCU internal pull-ups (parallel with the board's).
 *     With both, a speed sweep is clean 10k..400k; we run 200 kHz for margin.
 *     WHO_AM_I (reg 0x75) reads 0x68 — that's the chip ID, not the 0x69 address.
 *   - SCL=PC5 (MCU 28), SDA=PC4 (MCU 27). F_CPU=16 MHz (set in Makefile).
 */
#include <avr/io.h>
#include <util/delay.h>
#include <stdint.h>

#define ADDR 0x69                  /* AD0 high */

/* ---- UART (PD1/TXD) ---- */
static void uart_init(void)
{
    UBRR0H = 0; UBRR0L = 34;        /* 57600 @16MHz, U2X */
    UCSR0A = (1 << U2X0);
    UCSR0B = (1 << TXEN0);
    UCSR0C = (1 << UCSZ01) | (1 << UCSZ00);
}
static void uart_tx(char c) { while (!(UCSR0A & (1 << UDRE0))) ; UDR0 = c; }
static void uart_str(const char *s) { while (*s) uart_tx(*s++); }
static void uart_hex(uint8_t b)
{
    const char *h = "0123456789ABCDEF";
    uart_tx('0'); uart_tx('x'); uart_tx(h[b >> 4]); uart_tx(h[b & 0xF]);
}
static void uart_i16(int16_t v)
{
    char b[6]; uint8_t i = 0; uint16_t u;
    if (v < 0) { uart_tx('-'); u = -v; } else u = v;
    do { b[i++] = '0' + u % 10; u /= 10; } while (u);
    while (i) uart_tx(b[--i]);
}

/* ---- I2C lockup recovery: clock SCL until the slave releases SDA, then STOP ---- */
static void bus_recover(void)
{
    TWCR = 0;
    PORTC |= (1 << PC4) | (1 << PC5);          /* internal pull-ups */
    DDRC  &= ~((1 << PC4) | (1 << PC5));        /* release both lines */
    _delay_us(20);
    for (uint8_t i = 0; i < 16 && !(PINC & (1 << PC4)); i++) {
        DDRC |= (1 << PC5);  _delay_us(40);    /* SCL low  (~12 kHz) */
        DDRC &= ~(1 << PC5); _delay_us(40);    /* SCL high */
    }
    DDRC |= (1 << PC4);  _delay_us(40);        /* STOP: SDA low.. */
    DDRC &= ~(1 << PC5); _delay_us(40);        /* SCL high */
    DDRC &= ~(1 << PC4); _delay_us(40);        /* ..SDA high */
}

/* ---- TWI @ 200 kHz ---- */
static void twi_on(void)
{
    PORTC |= (1 << PC4) | (1 << PC5);          /* keep internal pull-ups */
    TWSR = 0x00; TWBR = 32; TWCR = (1 << TWEN);   /* prescaler 1 -> 200 kHz */
    _delay_us(100);
}
static uint8_t tw(uint8_t cr)
{
    uint16_t t = 0;
    TWCR = cr;
    while (!(TWCR & (1 << TWINT))) if (++t == 0) return 0xFF;
    return TWSR & 0xF8;
}
#define ST()  tw((1 << TWINT) | (1 << TWSTA) | (1 << TWEN))
#define WR(v) (TWDR = (v), tw((1 << TWINT) | (1 << TWEN)))
static void STPc(void) { TWCR = (1 << TWINT) | (1 << TWSTO) | (1 << TWEN); _delay_us(200); }

static uint8_t mpu_read(uint8_t reg, uint8_t *buf, uint8_t len)
{
    if (ST() != 0x08) return 1;
    if (WR((ADDR << 1) | 0) != 0x18) { STPc(); return 1; }
    if (WR(reg) != 0x28) { STPc(); return 1; }
    if (ST() != 0x10) { STPc(); return 1; }
    if (WR((ADDR << 1) | 1) != 0x40) { STPc(); return 1; }
    for (uint8_t i = 0; i < len; i++) {
        tw((1 << TWINT) | (1 << TWEN) | (i < len - 1 ? (1 << TWEA) : 0));
        buf[i] = TWDR;
    }
    STPc();
    return 0;
}
static uint8_t mpu_write(uint8_t reg, uint8_t val)
{
    if (ST() != 0x08) return 1;
    if (WR((ADDR << 1) | 0) != 0x18) { STPc(); return 1; }
    if (WR(reg) != 0x28) { STPc(); return 1; }
    if (WR(val) != 0x28) { STPc(); return 1; }
    STPc();
    return 0;
}
static int16_t be16(const uint8_t *p) { return (int16_t)((p[0] << 8) | p[1]); }

int main(void)
{
    uint8_t raw[14], who = 0;

    uart_init();
    uart_str("\r\nA3FC MPU @0x69\r\n");

    bus_recover();
    twi_on();
    mpu_write(0x6B, 0x00);         /* wake: clear SLEEP */
    _delay_ms(50);
    mpu_read(0x75, &who, 1);
    uart_str("WHO_AM_I="); uart_hex(who); uart_str("\r\n");

    for (;;) {
        if (mpu_read(0x3B, raw, 14) == 0) {     /* accel(6) temp(2) gyro(6) */
            uart_str("A ");
            uart_i16(be16(raw));      uart_tx(' ');
            uart_i16(be16(raw + 2));  uart_tx(' ');
            uart_i16(be16(raw + 4));
            uart_str("  G ");
            uart_i16(be16(raw + 8));  uart_tx(' ');
            uart_i16(be16(raw + 10)); uart_tx(' ');
            uart_i16(be16(raw + 12));
            uart_str("\r\n");
        } else {
            uart_str("read err -> recover\r\n");
            bus_recover(); twi_on();
        }
        _delay_ms(100);
    }
}
