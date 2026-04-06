#pragma once

#include <stdint.h>

#include "system/render/render_common.h"

void render_text_init_display(void);
uint8_t render_text_queue_request(render_request_t *request,
                                  const char *line0,
                                  const char *line1,
                                  const char *line2,
                                  const char *line3);
uint8_t render_text_process_request(const render_request_t *request);
uint8_t render_text_show_image_load_error(const char *name);
