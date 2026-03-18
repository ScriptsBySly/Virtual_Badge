#pragma once

#include <stdint.h>

#ifndef HAL_HAS_SEPARATE_SPI_BUSES
#define HAL_HAS_SEPARATE_SPI_BUSES 0
#endif

void hal_init(void);

void hal_delay_ms(uint16_t ms);

void hal_spi_tft_init(void);
void hal_spi_sd_init(void);

void hal_spi_tft_set_speed_fast(void);
void hal_spi_tft_write(uint8_t data);
void hal_spi_tft_write_buffer(const uint8_t *data, uint16_t len);

void hal_spi_sd_set_speed_fast(void);
void hal_spi_sd_set_speed_very_slow(void);
uint8_t hal_spi_sd_transfer(uint8_t data);
void hal_spi_sd_read_buffer(uint8_t *data, uint16_t len);

void hal_uart_init(void);
void hal_uart_putc(char c);
void hal_uart_put_hex8(uint8_t v);
uint8_t hal_uart_getc_nonblock(char *out);

void hal_sd_cs_low(void);
void hal_sd_cs_high(void);

void hal_tft_cs_low(void);
void hal_tft_cs_high(void);
void hal_tft_dc_low(void);
void hal_tft_dc_high(void);
void hal_tft_rst_low(void);
void hal_tft_rst_high(void);

uint8_t hal_miso_state(void);
