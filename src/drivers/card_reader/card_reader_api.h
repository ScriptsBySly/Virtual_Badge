#pragma once

#include <stdint.h>

#include "drivers/card_reader/card_reader_common.h"

card_reader_state_t *card_reader_file_open(void);
card_reader_state_t *card_reader_wait_ready(card_reader_wait_status_fn_t status_fn, void *ctx);
uint8_t card_reader_file_read(card_reader_state_t *dev,
const char *name,
uint32_t expected_size,
void (*sink)(const uint8_t *data, uint16_t len, void *ctx),
void *ctx);
uint8_t card_reader_file_write(card_reader_state_t *dev,
const char *name,
const uint8_t *data,
uint32_t len);
void card_reader_file_close(card_reader_state_t *dev);
void card_reader_describe_status(const card_reader_state_t *dev,
char line0[CARD_READER_STATUS_LINE_CAPACITY],
char line1[CARD_READER_STATUS_LINE_CAPACITY],
char line2[CARD_READER_STATUS_LINE_CAPACITY],
char line3[CARD_READER_STATUS_LINE_CAPACITY]);
