#pragma once

#include <stdint.h>

#include "system/render/render_common.h"

uint8_t render_image_queue_request(render_request_t *request,
                                   const char *name,
                                   uint16_t width,
                                   uint16_t height);
uint8_t render_image_queue_preload_secondary_request(render_request_t *request,
                                                     const char *name,
                                                     uint16_t width,
                                                     uint16_t height);
uint8_t render_image_queue_preload_secondary_list_request(render_request_t *request,
                                                          const char *const *names,
                                                          uint8_t count,
                                                          uint16_t width,
                                                          uint16_t height);
uint8_t render_image_preload_primary(render_state_t *state,
                                     const char *name,
                                     uint16_t width,
                                     uint16_t height);
uint8_t render_image_preload_secondary(render_state_t *state,
                                       const char *name,
                                       uint16_t width,
                                       uint16_t height);
uint8_t render_image_process_request(render_state_t *state, const render_request_t *request);
