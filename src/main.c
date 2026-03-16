#ifndef F_CPU
#define F_CPU 16000000UL
#endif

#include <avr/io.h>
#include <avr/pgmspace.h>
#include <stdint.h>
#include <util/delay.h>

#include "generated_images.h"

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
#ifndef TFT_WIDTH
#define TFT_WIDTH 128u
#endif
#ifndef TFT_HEIGHT
#define TFT_HEIGHT 160u
#endif
#ifndef TFT_X_OFFSET
#define TFT_X_OFFSET 0u
#endif
#ifndef TFT_Y_OFFSET
#define TFT_Y_OFFSET 0u
#endif

static void spi_init(void) {
    // MOSI, SCK, SS as output
    DDRB |= (1 << PB3) | (1 << PB5) | (1 << PB2);
    // Enable SPI, Master, fosc/2 (SPI2X=1, SPR1:0=00)
    SPCR = (1 << SPE) | (1 << MSTR);
    SPSR = (1 << SPI2X);
}

static void spi_write(uint8_t data) {
    SPDR = data;
    while (!(SPSR & (1 << SPIF))) {
        // wait
    }
}

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

static void tft_init(void) {
    // Control pins as output
    TFT_CS_DDR |= (1 << TFT_CS_PIN);
    TFT_DC_DDR |= (1 << TFT_DC_PIN);
    TFT_RST_DDR |= (1 << TFT_RST_PIN);

    TFT_CS_HIGH();
    TFT_DC_HIGH();
    TFT_RST_HIGH();

    spi_init();
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

static void tft_fill_color(uint16_t color) {
    tft_set_addr_window(0, 0, TFT_WIDTH - 1, TFT_HEIGHT - 1);

    TFT_DC_HIGH();
    TFT_CS_LOW();
    uint16_t pixels = (uint16_t)(TFT_WIDTH * TFT_HEIGHT);
    uint8_t hi = (uint8_t)(color >> 8);
    uint8_t lo = (uint8_t)(color & 0xFF);
    while (pixels--) {
        spi_write(hi);
        spi_write(lo);
    }
    TFT_CS_HIGH();
}

static void tft_draw_rle_image_P(const uint16_t *img_rle, uint16_t width, uint16_t height) {
    tft_set_addr_window(0, 0, width - 1, height - 1);

    TFT_DC_HIGH();
    TFT_CS_LOW();
    uint32_t remaining = (uint32_t)width * (uint32_t)height;
    uint32_t i = 0;
    while (remaining) {
        uint16_t run_len = pgm_read_word(&img_rle[i++]);
        uint16_t color = pgm_read_word(&img_rle[i++]);
        uint8_t hi = (uint8_t)(color >> 8);
        uint8_t lo = (uint8_t)(color & 0xFF);
        for (uint16_t j = 0; j < run_len; j++) {
            spi_write(hi);
            spi_write(lo);
        }
        remaining -= (uint32_t)run_len;
    }
    TFT_CS_HIGH();
}

int main(void) {
    tft_init();
    tft_fill_color(0x0000);

    while (1) {
        tft_draw_rle_image_P(IMG_HD_EO_MC_RLE, IMG_HD_EO_MC_WIDTH, IMG_HD_EO_MC_HEIGHT);
        _delay_ms(500);
        tft_draw_rle_image_P(IMG_HU_EO_MC_RLE, IMG_HU_EO_MC_WIDTH, IMG_HU_EO_MC_HEIGHT);
        _delay_ms(500);
    }
}
