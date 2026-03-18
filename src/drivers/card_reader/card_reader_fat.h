#pragma once

#include <stdint.h>

#include "drivers/card_reader/card_reader_common.h"

uint8_t card_reader_fat_mount(card_reader_state_t *state);
uint8_t card_reader_fat_is_formatted(card_reader_state_t *state);
uint8_t card_reader_fat_find_file_root(card_reader_state_t *state,
const char *name,
uint32_t *first_cluster,
uint32_t *file_size);
uint8_t card_reader_fat_read_file_stream_ctx(card_reader_state_t *state,
const char *name,
uint32_t expected_size,
void (*sink)(const uint8_t *data, uint16_t len, void *ctx),
void *ctx);
