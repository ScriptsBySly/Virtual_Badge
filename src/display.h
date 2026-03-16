#pragma once

#include <stdint.h>

void display_init(void);
void display_fill_color(uint16_t color);
void display_set_addr_window(uint16_t width, uint16_t height);
void display_stream_bytes(const uint8_t *data, uint16_t len);
