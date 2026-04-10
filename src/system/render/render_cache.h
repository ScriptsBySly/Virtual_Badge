#pragma once

#include <stdint.h>

#include "system/render/render_common.h"

void render_cache_init(render_state_t *state);
void render_cache_reset_primary(render_state_t *state);
void render_cache_reset_secondary(render_state_t *state);
void render_cache_reset_all(render_state_t *state);
render_cache_entry_t *render_cache_find_any(render_state_t *state, const char *name, uint32_t expected_size);
uint8_t render_cache_store_primary(render_state_t *state, const char *name, const uint8_t *data, uint32_t size);
uint8_t render_cache_store_secondary(render_state_t *state, const char *name, const uint8_t *data, uint32_t size);
