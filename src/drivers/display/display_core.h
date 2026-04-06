#pragma once

#include <stdint.h>

void display_core_init(void);
void display_core_fill_color(uint16_t color);
void display_core_set_addr_window(uint16_t width, uint16_t height);
void display_core_stream_bytes(const uint8_t *data, uint16_t len);
void display_core_draw_char(uint16_t x, uint16_t y, char c, uint16_t fg, uint16_t bg);
void display_core_draw_text(uint16_t x, uint16_t y, const char *text, uint16_t fg, uint16_t bg);
