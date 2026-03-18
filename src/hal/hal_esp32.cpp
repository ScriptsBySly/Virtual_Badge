#include "hal.h"

#include <Arduino.h>
#include <SPI.h>

#include "hal_esp32_target_select.h"

#ifndef BAUD
#define BAUD 115200UL
#endif

static SPIClass spi(VSPI);
static SPISettings spi_settings_fast(40000000, MSBFIRST, SPI_MODE0);
static SPISettings spi_settings_slow(1000000, MSBFIRST, SPI_MODE0);
static uint8_t spi_ready = 0;

extern "C" {

void hal_init(void) {
    Serial.begin(BAUD);
    hal_spi_tft_init();
    hal_spi_sd_init();

    pinMode(HAL_ESP32_TFT_CS, OUTPUT);
    pinMode(HAL_ESP32_TFT_DC, OUTPUT);
    pinMode(HAL_ESP32_TFT_RST, OUTPUT);
    pinMode(HAL_ESP32_SD_CS, OUTPUT);

    hal_tft_cs_high();
    hal_tft_dc_high();
    hal_tft_rst_high();
    hal_sd_cs_high();
}

void hal_delay_ms(uint16_t ms) {
    delay(ms);
}

static void spi_begin_if_needed(void) {
    if (spi_ready) {
        return;
    }
    spi.begin(HAL_ESP32_SPI_SCK, HAL_ESP32_SPI_MISO, HAL_ESP32_SPI_MOSI, -1);
    spi_ready = 1;
}

void hal_spi_tft_init(void) {
    spi_begin_if_needed();
    spi.endTransaction();
    spi.beginTransaction(spi_settings_fast);
}

void hal_spi_sd_init(void) {
    spi_begin_if_needed();
    spi.endTransaction();
    spi.beginTransaction(spi_settings_fast);
}

void hal_spi_tft_set_speed_fast(void) {
    spi.endTransaction();
    spi.beginTransaction(spi_settings_fast);
}

void hal_spi_sd_set_speed_fast(void) {
    spi.endTransaction();
    spi.beginTransaction(spi_settings_fast);
}

void hal_spi_sd_set_speed_very_slow(void) {
    spi.endTransaction();
    spi.beginTransaction(spi_settings_slow);
}

uint8_t hal_spi_sd_transfer(uint8_t data) {
    return spi.transfer(data);
}

void hal_spi_tft_write(uint8_t data) {
    spi.transfer(data);
}

void hal_spi_tft_write_buffer(const uint8_t *data, uint16_t len) {
    if (!data || len == 0) {
        return;
    }
    spi.transferBytes((uint8_t *)data, nullptr, len);
}

void hal_spi_sd_read_buffer(uint8_t *data, uint16_t len) {
    if (!data || len == 0) {
        return;
    }
    static uint8_t ff = 0xFF;
    for (uint16_t i = 0; i < len; i++) {
        data[i] = spi.transfer(ff);
    }
}

void hal_uart_init(void) {
    Serial.begin(BAUD);
}

void hal_uart_putc(char c) {
    Serial.write((uint8_t)c);
}

void hal_uart_put_hex8(uint8_t v) {
    const char hex[] = "0123456789ABCDEF";
    Serial.write(hex[(v >> 4) & 0xF]);
    Serial.write(hex[v & 0xF]);
}

uint8_t hal_uart_getc_nonblock(char *out) {
    if (Serial.available() > 0) {
        *out = (char)Serial.read();
        return 1;
    }
    return 0;
}

void hal_sd_cs_low(void) { digitalWrite(HAL_ESP32_SD_CS, LOW); }
void hal_sd_cs_high(void) { digitalWrite(HAL_ESP32_SD_CS, HIGH); }

void hal_tft_cs_low(void) { digitalWrite(HAL_ESP32_TFT_CS, LOW); }
void hal_tft_cs_high(void) { digitalWrite(HAL_ESP32_TFT_CS, HIGH); }
void hal_tft_dc_low(void) { digitalWrite(HAL_ESP32_TFT_DC, LOW); }
void hal_tft_dc_high(void) { digitalWrite(HAL_ESP32_TFT_DC, HIGH); }
void hal_tft_rst_low(void) { digitalWrite(HAL_ESP32_TFT_RST, LOW); }
void hal_tft_rst_high(void) { digitalWrite(HAL_ESP32_TFT_RST, HIGH); }

uint8_t hal_miso_state(void) {
    return digitalRead(HAL_ESP32_SD_SPI_MISO) ? 1 : 0;
}

} // extern "C"
