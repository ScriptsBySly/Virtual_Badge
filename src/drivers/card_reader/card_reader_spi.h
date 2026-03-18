#pragma once

#include <stdint.h>

typedef enum {
    CARD_READER_SPI_SPEED_FAST,
    CARD_READER_SPI_SPEED_SLOW,
} card_reader_spi_speed_t;

void card_reader_spi_deselect(void);
void card_reader_spi_idle_clocks(uint8_t count);
uint8_t card_reader_spi_select(void);
uint8_t card_reader_spi_wait_ready(uint16_t timeout_ms);
uint8_t card_reader_spi_send_cmd(uint8_t cmd,
uint32_t arg,
uint8_t crc,
uint8_t *response_r7,
uint8_t skip_select);
void card_reader_spi_set_speed(card_reader_spi_speed_t speed);
uint8_t card_reader_spi_transfer_byte(uint8_t data);
void card_reader_spi_read_buffer(uint8_t *data, uint16_t len);
