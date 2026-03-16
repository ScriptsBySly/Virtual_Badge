#pragma once

#include <stdint.h>

typedef struct {
    uint8_t sd_ok;
    uint8_t fat_ok;
} card_reader_status_t;

void spi_init(void);
void spi_set_speed_fast(void);
void spi_set_speed_very_slow(void);
uint8_t spi_transfer(uint8_t data);
void spi_write(uint8_t data);

void card_reader_init(card_reader_status_t *status);
void card_reader_print_status(const card_reader_status_t *status);
void card_reader_handle_cli(const card_reader_status_t *status,
                            const char *image1,
                            const char *image2,
                            uint16_t width,
                            uint16_t height);
uint8_t card_reader_draw_raw565(const char *name, uint16_t width, uint16_t height);
