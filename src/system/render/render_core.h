#pragma once

#include <stdint.h>

#include "drivers/card_reader/card_reader_common.h"

void render_core_init(void);
uint8_t render_core_app_task(void *ctx);
void render_core_bind_reader(card_reader_state_t *dev);
void render_core_reset_caches(void);
void render_core_reset_secondary_cache(void);
void render_core_drop_pending_draws(void);
uint8_t render_core_secondary_preload_ready(void);
uint8_t render_core_secondary_has_raw565(const char *name, uint16_t width, uint16_t height);
uint8_t render_core_queue_raw565(const char *name, uint16_t width, uint16_t height);
uint8_t render_core_queue_preload_raw565_secondary(const char *name, uint16_t width, uint16_t height);
uint8_t render_core_queue_preload_raw565_secondary_list(const char *const *names,
                                                        uint8_t count,
                                                        uint16_t width,
                                                        uint16_t height);
uint8_t render_core_queue_reset_secondary_cache(void);
uint8_t render_core_preload_raw565_primary(const char *name, uint16_t width, uint16_t height);
uint8_t render_core_preload_raw565_secondary(const char *name, uint16_t width, uint16_t height);
uint8_t render_core_show_text_screen(const char *line0,
                                     const char *line1,
                                     const char *line2,
                                     const char *line3);
