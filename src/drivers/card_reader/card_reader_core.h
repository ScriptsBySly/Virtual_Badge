#pragma once

#include "drivers/card_reader/card_reader_common.h"

card_reader_state_t *card_reader_core_open(void);
uint8_t card_reader_core_read(card_reader_state_t *dev,
const char *name,
uint32_t expected_size,
void (*sink)(const uint8_t *data, uint16_t len, void *ctx),
void *ctx);
uint8_t card_reader_core_write(card_reader_state_t *dev,
const char *name,
const uint8_t *data,
uint32_t len);
void card_reader_core_close(card_reader_state_t *dev);
