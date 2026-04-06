#pragma once

#include "drivers/card_reader/card_reader_common.h"

card_reader_state_t *card_reader_core_open(void);
card_reader_state_t *card_reader_core_wait_ready(card_reader_wait_status_fn_t status_fn, void *ctx);
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
void card_reader_core_describe_status(const card_reader_state_t *dev,
char *line0,
char *line1,
char *line2,
char *line3);
