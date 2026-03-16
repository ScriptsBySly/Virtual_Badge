#include "display.h"

#include <avr/io.h>
#include <util/delay.h>

#include "card_reader.h"

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

#define TFT_CS_LOW()   (TFT_CS_PORT &= ~(1 << TFT_CS_PIN))
#define TFT_CS_HIGH()  (TFT_CS_PORT |= (1 << TFT_CS_PIN))
#define TFT_DC_LOW()   (TFT_DC_PORT &= ~(1 << TFT_DC_PIN))
#define TFT_DC_HIGH()  (TFT_DC_PORT |= (1 << TFT_DC_PIN))
#define TFT_RST_LOW()  (TFT_RST_PORT &= ~(1 << TFT_RST_PIN))
#define TFT_RST_HIGH() (TFT_RST_PORT |= (1 << TFT_RST_PIN))

// Common ST7735 1.8" 128x160 defaults.
#ifndef TFT_X_OFFSET
#define TFT_X_OFFSET 0u
#endif
#ifndef TFT_Y_OFFSET
#define TFT_Y_OFFSET 0u
#endif

static void tft_write_cmd(uint8_t cmd) {
    TFT_DC_LOW();
    TFT_CS_LOW();
    spi_write(cmd);
    TFT_CS_HIGH();
}

static void tft_write_data(uint8_t data) {
    TFT_DC_HIGH();
    TFT_CS_LOW();
    spi_write(data);
    TFT_CS_HIGH();
}

static void tft_reset(void) {
    TFT_RST_LOW();
    _delay_ms(20);
    TFT_RST_HIGH();
    _delay_ms(150);
}

static void tft_write_u16(uint16_t v) {
    tft_write_data((uint8_t)(v >> 8));
    tft_write_data((uint8_t)(v & 0xFF));
}

static void tft_set_addr_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1) {
    x0 += TFT_X_OFFSET;
    x1 += TFT_X_OFFSET;
    y0 += TFT_Y_OFFSET;
    y1 += TFT_Y_OFFSET;

    tft_write_cmd(0x2A); // CASET
    tft_write_u16(x0);
    tft_write_u16(x1);

    tft_write_cmd(0x2B); // RASET
    tft_write_u16(y0);
    tft_write_u16(y1);

    tft_write_cmd(0x2C); // RAMWR
}

void display_init(void) {
    // Control pins as output
    TFT_CS_DDR |= (1 << TFT_CS_PIN);
    TFT_DC_DDR |= (1 << TFT_DC_PIN);
    TFT_RST_DDR |= (1 << TFT_RST_PIN);

    TFT_CS_HIGH();
    TFT_DC_HIGH();
    TFT_RST_HIGH();

    spi_init();
    spi_set_speed_fast();
    tft_reset();

    // Basic ST7735 init sequence (common for 128x160)
    tft_write_cmd(0x01); // SWRESET
    _delay_ms(150);

    tft_write_cmd(0x11); // SLPOUT
    _delay_ms(150);

    tft_write_cmd(0x3A); // COLMOD: 16-bit color
    tft_write_data(0x05);

    tft_write_cmd(0x36); // MADCTL
    tft_write_data(0x00);

    tft_write_cmd(0x29); // DISPON
    _delay_ms(50);
}

void display_fill_color(uint16_t color) {
    display_set_addr_window(128, 160);

    TFT_DC_HIGH();
    TFT_CS_LOW();
    uint16_t pixels = (uint16_t)(128u * 160u);
    uint8_t hi = (uint8_t)(color >> 8);
    uint8_t lo = (uint8_t)(color & 0xFF);
    while (pixels--) {
        spi_write(hi);
        spi_write(lo);
    }
    TFT_CS_HIGH();
}

void display_set_addr_window(uint16_t width, uint16_t height) {
    tft_set_addr_window(0, 0, width - 1, height - 1);
}

void display_stream_bytes(const uint8_t *data, uint16_t len) {
    TFT_DC_HIGH();
    TFT_CS_LOW();
    for (uint16_t i = 0; i < len; i++) {
        spi_write(data[i]);
    }
    TFT_CS_HIGH();
}
