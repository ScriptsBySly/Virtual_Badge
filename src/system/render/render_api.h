#pragma once

#include <stdint.h>

#include "drivers/card_reader/card_reader_common.h"

void render_init(void);
uint8_t render_app_task(void *ctx);
void render_bind_reader(card_reader_state_t *dev);
uint8_t render_queue_raw565(const char *name, uint16_t width, uint16_t height);
uint8_t render_show_text_screen(const char *line0,
                                const char *line1,
                                const char *line2,
                                const char *line3);
