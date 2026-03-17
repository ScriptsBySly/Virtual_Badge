#include "hal.h"

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "driver/uart.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_err.h"
#include <string.h>

#ifndef BAUD
#define BAUD 115200UL
#endif

#ifndef HAL_ESP32_SPI_SCK
#define HAL_ESP32_SPI_SCK 18
#endif
#ifndef HAL_ESP32_SPI_MISO
#define HAL_ESP32_SPI_MISO 19
#endif
#ifndef HAL_ESP32_SPI_MOSI
#define HAL_ESP32_SPI_MOSI 23
#endif

#ifndef HAL_ESP32_TFT_CS
#define HAL_ESP32_TFT_CS 16
#endif
#ifndef HAL_ESP32_TFT_DC
#define HAL_ESP32_TFT_DC 17
#endif
#ifndef HAL_ESP32_TFT_RST
#define HAL_ESP32_TFT_RST 25
#endif

#ifndef HAL_ESP32_SD_CS
#define HAL_ESP32_SD_CS 27
#endif

#define SPI_FAST_HZ 20000000
#define SPI_SLOW_HZ 400000

static spi_device_handle_t spi_dev = NULL;
static uint8_t spi_ready = 0;

static void spi_setup(int clock_hz) {
    if (!spi_ready) {
        spi_bus_config_t buscfg = {
            .miso_io_num = HAL_ESP32_SPI_MISO,
            .mosi_io_num = HAL_ESP32_SPI_MOSI,
            .sclk_io_num = HAL_ESP32_SPI_SCK,
            .quadwp_io_num = -1,
            .quadhd_io_num = -1,
            .max_transfer_sz = 0,
        };
        (void)spi_bus_initialize(VSPI_HOST, &buscfg, SPI_DMA_CH_AUTO);
        spi_ready = 1;
    }

    if (spi_dev) {
        (void)spi_bus_remove_device(spi_dev);
        spi_dev = NULL;
    }

    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = clock_hz,
        .mode = 0,
        .spics_io_num = -1,
        .queue_size = 1,
    };
    (void)spi_bus_add_device(VSPI_HOST, &devcfg, &spi_dev);
}

void hal_init(void) {
    hal_uart_init();
    hal_spi_init();

    gpio_config_t io_cfg = {
        .pin_bit_mask = (1ULL << HAL_ESP32_TFT_CS) |
                        (1ULL << HAL_ESP32_TFT_DC) |
                        (1ULL << HAL_ESP32_TFT_RST) |
                        (1ULL << HAL_ESP32_SD_CS),
        .mode = GPIO_MODE_OUTPUT,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_cfg);

    hal_tft_cs_high();
    hal_tft_dc_high();
    hal_tft_rst_high();
    hal_sd_cs_high();
}

void hal_delay_ms(uint16_t ms) {
    vTaskDelay(pdMS_TO_TICKS(ms));
}

void hal_spi_init(void) {
    spi_setup(SPI_FAST_HZ);
}

void hal_spi_set_speed_fast(void) {
    spi_setup(SPI_FAST_HZ);
}

void hal_spi_set_speed_very_slow(void) {
    spi_setup(SPI_SLOW_HZ);
}

uint8_t hal_spi_transfer(uint8_t data) {
    uint8_t rx = 0xFF;
    spi_transaction_t t = {0};
    t.length = 8;
    t.tx_buffer = &data;
    t.rx_buffer = &rx;
    (void)spi_device_transmit(spi_dev, &t);
    return rx;
}

void hal_spi_write(uint8_t data) {
    spi_transaction_t t = {0};
    t.length = 8;
    t.tx_buffer = &data;
    (void)spi_device_transmit(spi_dev, &t);
}

void hal_uart_init(void) {
    uart_config_t cfg = {
        .baud_rate = (int)BAUD,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_APB,
    };
    uart_param_config(UART_NUM_0, &cfg);
    uart_driver_install(UART_NUM_0, 1024, 0, 0, NULL, 0);
}

void hal_uart_putc(char c) {
    uart_write_bytes(UART_NUM_0, &c, 1);
}

void hal_uart_puts(const char *s) {
    if (!s) {
        return;
    }
    uart_write_bytes(UART_NUM_0, s, (size_t)strlen(s));
}

void hal_uart_put_hex8(uint8_t v) {
    const char hex[] = "0123456789ABCDEF";
    char out[2] = { hex[(v >> 4) & 0xF], hex[v & 0xF] };
    uart_write_bytes(UART_NUM_0, out, 2);
}

uint8_t hal_uart_getc_nonblock(char *out) {
    if (!out) {
        return 0;
    }
    int len = uart_read_bytes(UART_NUM_0, (uint8_t *)out, 1, 0);
    return (len == 1) ? 1 : 0;
}

void hal_sd_cs_low(void) { gpio_set_level(HAL_ESP32_SD_CS, 0); }
void hal_sd_cs_high(void) { gpio_set_level(HAL_ESP32_SD_CS, 1); }

void hal_tft_cs_low(void) { gpio_set_level(HAL_ESP32_TFT_CS, 0); }
void hal_tft_cs_high(void) { gpio_set_level(HAL_ESP32_TFT_CS, 1); }
void hal_tft_dc_low(void) { gpio_set_level(HAL_ESP32_TFT_DC, 0); }
void hal_tft_dc_high(void) { gpio_set_level(HAL_ESP32_TFT_DC, 1); }
void hal_tft_rst_low(void) { gpio_set_level(HAL_ESP32_TFT_RST, 0); }
void hal_tft_rst_high(void) { gpio_set_level(HAL_ESP32_TFT_RST, 1); }

uint8_t hal_miso_state(void) {
    return gpio_get_level(HAL_ESP32_SPI_MISO) ? 1 : 0;
}
