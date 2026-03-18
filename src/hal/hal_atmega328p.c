#ifndef F_CPU
#define F_CPU 16000000UL
#endif

#ifndef BAUD
#define BAUD 9600UL
#endif

#include "hal.h"

#include <avr/io.h>
#include <util/delay.h>
#include <util/setbaud.h>

// Pin mapping for Nano (SPI):
// SCK = D13 (PB5), MOSI = D11 (PB3), CS = D10 (PB2)
// RS/DC = D9 (PB1), RST = D8 (PB0)
#define TFT_CS_PORT PORTB
#define TFT_CS_DDR  DDRB
#define TFT_CS_PIN  PB2

#define TFT_DC_PORT PORTB
#define TFT_DC_DDR  DDRB
#define TFT_DC_PIN  PB1

#define TFT_RST_PORT PORTB
#define TFT_RST_DDR  DDRB
#define TFT_RST_PIN  PB0

// SD card (SPI) on Nano: CS = D4 (PD4), SCK = D13 (PB5), MOSI = D11 (PB3), MISO = D12 (PB4)
#define SD_CS_PORT  PORTD
#define SD_CS_DDR   DDRD
#define SD_CS_PIN   PD4

void hal_init(void) {
    hal_spi_tft_init();
    hal_spi_sd_init();
    hal_uart_init();

    // TFT control pins as output
    TFT_CS_DDR |= (1 << TFT_CS_PIN);
    TFT_DC_DDR |= (1 << TFT_DC_PIN);
    TFT_RST_DDR |= (1 << TFT_RST_PIN);

    // SD CS as output
    SD_CS_DDR |= (1 << SD_CS_PIN);

    hal_tft_cs_high();
    hal_tft_dc_high();
    hal_tft_rst_high();
    hal_sd_cs_high();
}

void hal_delay_ms(uint16_t ms) {
    while (ms--) {
        _delay_ms(1);
    }
}

static void hal_spi_shared_init(void) {
    // MOSI, SCK, SS as output. MISO input.
    DDRB |= (1 << PB3) | (1 << PB5) | (1 << PB2);
    DDRB &= ~(1 << PB4);
    PORTB |= (1 << PB4); // pull-up on MISO
    PORTB |= (1 << PB2); // keep SS high (stay master)
    // Enable SPI, Master, mode 0.
    SPCR = (1 << SPE) | (1 << MSTR);
    SPSR = 0;
}

void hal_spi_tft_init(void) {
    hal_spi_shared_init();
}

void hal_spi_sd_init(void) {
    hal_spi_shared_init();
}

static void hal_spi_shared_set_speed_very_slow(void) {
    // fosc/256
    SPCR |= (1 << SPR1) | (1 << SPR0);
    SPSR &= ~(1 << SPI2X);
}

static void hal_spi_shared_set_speed_fast(void) {
    // fosc/2
    SPCR &= ~((1 << SPR1) | (1 << SPR0));
    SPSR |= (1 << SPI2X);
}

void hal_spi_tft_set_speed_fast(void) {
    hal_spi_shared_set_speed_fast();
}

void hal_spi_sd_set_speed_fast(void) {
    hal_spi_shared_set_speed_fast();
}

void hal_spi_sd_set_speed_very_slow(void) {
    hal_spi_shared_set_speed_very_slow();
}

uint8_t hal_spi_sd_transfer(uint8_t data) {
    SPDR = data;
    while (!(SPSR & (1 << SPIF))) {
    }
    return SPDR;
}

void hal_spi_tft_write(uint8_t data) {
    (void)hal_spi_sd_transfer(data);
}

void hal_spi_tft_write_buffer(const uint8_t *data, uint16_t len) {
    for (uint16_t i = 0; i < len; i++) {
        hal_spi_tft_write(data[i]);
    }
}

void hal_spi_sd_read_buffer(uint8_t *data, uint16_t len) {
    for (uint16_t i = 0; i < len; i++) {
        data[i] = hal_spi_sd_transfer(0xFF);
    }
}

void hal_uart_init(void) {
    UBRR0H = UBRRH_VALUE;
    UBRR0L = UBRRL_VALUE;
#if USE_2X
    UCSR0A = (1 << U2X0);
#else
    UCSR0A = 0;
#endif
    UCSR0B = (1 << TXEN0) | (1 << RXEN0);
    UCSR0C = (1 << UCSZ01) | (1 << UCSZ00);
}

void hal_uart_putc(char c) {
    while (!(UCSR0A & (1 << UDRE0))) {
    }
    UDR0 = (uint8_t)c;
}

void hal_uart_puts(const char *s) {
    while (*s) {
        hal_uart_putc(*s++);
    }
}

void hal_uart_put_hex8(uint8_t v) {
    const char hex[] = "0123456789ABCDEF";
    hal_uart_putc(hex[(v >> 4) & 0xF]);
    hal_uart_putc(hex[v & 0xF]);
}

uint8_t hal_uart_getc_nonblock(char *out) {
    if (UCSR0A & (1 << RXC0)) {
        *out = (char)UDR0;
        return 1;
    }
    return 0;
}

void hal_sd_cs_low(void) { SD_CS_PORT &= ~(1 << SD_CS_PIN); }
void hal_sd_cs_high(void) { SD_CS_PORT |= (1 << SD_CS_PIN); }

void hal_tft_cs_low(void) { TFT_CS_PORT &= ~(1 << TFT_CS_PIN); }
void hal_tft_cs_high(void) { TFT_CS_PORT |= (1 << TFT_CS_PIN); }
void hal_tft_dc_low(void) { TFT_DC_PORT &= ~(1 << TFT_DC_PIN); }
void hal_tft_dc_high(void) { TFT_DC_PORT |= (1 << TFT_DC_PIN); }
void hal_tft_rst_low(void) { TFT_RST_PORT &= ~(1 << TFT_RST_PIN); }
void hal_tft_rst_high(void) { TFT_RST_PORT |= (1 << TFT_RST_PIN); }

uint8_t hal_miso_state(void) {
    return (PINB & (1 << PB4)) ? 1 : 0;
}
