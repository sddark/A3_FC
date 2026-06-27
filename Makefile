# A3_FC firmware Makefile (Raspberry Pi host, linuxspi ISP)
MCU        = atmega168p
F_CPU      = 16000000UL          # assumed crystal; blink rate will confirm
TARGET     = a3fc

# Programmer: hardware SPI0 + RESET on BCM25 via gpiochip0
PROGRAMMER = linuxspi
PORT       = /dev/spidev0.0:/dev/gpiochip0:25
BITCLOCK   = 250000

CC         = avr-gcc
OBJCOPY    = avr-objcopy
SIZE       = avr-size
CFLAGS     = -mmcu=$(MCU) -DF_CPU=$(F_CPU) -Os -Wall -Wextra -std=gnu11

RESET_GPIO = 25                  # BCM25 = ISP reset line on /dev/gpiochip0

.PHONY: all flash run release-reset fuses-app fuses-read clean

all: $(TARGET).hex

$(TARGET).elf: main.c
	$(CC) $(CFLAGS) -o $@ $<

$(TARGET).hex: $(TARGET).elf
	$(OBJCOPY) -O ihex -R .eeprom $< $@
	$(SIZE) --mcu=$(MCU) --format=avr $(TARGET).elf

# Write application firmware, then release reset so the MCU runs.
# avrdude's linuxspi leaves the reset GPIO driving LOW on exit (holds the AVR in
# reset); pinctrl puts it back to input+pull-up so the chip boots.
flash: $(TARGET).hex
	avrdude -c $(PROGRAMMER) -P $(PORT) -B $(BITCLOCK) -p $(MCU) -U flash:w:$(TARGET).hex:i
	$(MAKE) release-reset

run: release-reset

release-reset:
	pinctrl set $(RESET_GPIO) ip pu
	@pinctrl get $(RESET_GPIO)

# Set BOOTRST off so reset vectors to the application at 0x0000 (hfuse 0xd6 -> 0xd7).
# lfuse (crystal) and efuse (BOD) left at stock values.
fuses-app:
	avrdude -c $(PROGRAMMER) -P $(PORT) -B $(BITCLOCK) -p $(MCU) -U hfuse:w:0xd7:m

fuses-read:
	avrdude -c $(PROGRAMMER) -P $(PORT) -B $(BITCLOCK) -p $(MCU) \
	  -U lfuse:r:-:h -U hfuse:r:-:h -U efuse:r:-:h

clean:
	rm -f $(TARGET).elf $(TARGET).hex
