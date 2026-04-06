#pragma once

#include <stdint.h>

void display_spi_init(void);
void display_spi_set_addr_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1);
void display_spi_write_buffer(const uint8_t *data, uint16_t len);
