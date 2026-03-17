#include "display.h"

#include "hal/hal.h"

// Common ST7735 1.8" 128x160 defaults.
#ifndef TFT_X_OFFSET
#define TFT_X_OFFSET 0u
#endif
#ifndef TFT_Y_OFFSET
#define TFT_Y_OFFSET 0u
#endif

static void tft_write_cmd(uint8_t cmd) {
    hal_tft_dc_low();
    hal_tft_cs_low();
    hal_spi_write(cmd);
    hal_tft_cs_high();
}

static void tft_write_data(uint8_t data) {
    hal_tft_dc_high();
    hal_tft_cs_low();
    hal_spi_write(data);
    hal_tft_cs_high();
}

static void tft_reset(void) {
    hal_tft_rst_low();
    hal_delay_ms(20);
    hal_tft_rst_high();
    hal_delay_ms(150);
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
    hal_spi_set_speed_fast();
    tft_reset();

    // Basic ST7735 init sequence (common for 128x160)
    tft_write_cmd(0x01); // SWRESET
    hal_delay_ms(150);

    tft_write_cmd(0x11); // SLPOUT
    hal_delay_ms(150);

    tft_write_cmd(0x3A); // COLMOD: 16-bit color
    tft_write_data(0x05);

    tft_write_cmd(0x36); // MADCTL
    tft_write_data(0x00);

    tft_write_cmd(0x29); // DISPON
    hal_delay_ms(50);
}

void display_fill_color(uint16_t color) {
    display_set_addr_window(128, 160);

    hal_tft_dc_high();
    hal_tft_cs_low();
    uint16_t pixels = (uint16_t)(128u * 160u);
    uint8_t hi = (uint8_t)(color >> 8);
    uint8_t lo = (uint8_t)(color & 0xFF);
    while (pixels--) {
        hal_spi_write(hi);
        hal_spi_write(lo);
    }
    hal_tft_cs_high();
}

void display_set_addr_window(uint16_t width, uint16_t height) {
    tft_set_addr_window(0, 0, width - 1, height - 1);
}

void display_stream_bytes(const uint8_t *data, uint16_t len) {
    hal_tft_dc_high();
    hal_tft_cs_low();
    for (uint16_t i = 0; i < len; i++) {
        hal_spi_write(data[i]);
    }
    hal_tft_cs_high();
}
