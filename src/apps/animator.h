#pragma once

#include <stdint.h>

struct card_reader_state;
uint8_t animator_draw_raw565(struct card_reader_state *dev,
                             const char *name,
                             uint16_t width,
                             uint16_t height);

uint8_t animator_load_raw565(struct card_reader_state *dev,
                             const char *name,
                             uint16_t width,
                             uint16_t height,
                             uint8_t *dst,
                             uint32_t dst_size);
