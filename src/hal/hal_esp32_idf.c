#include "hal.h"

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "driver/uart.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_err.h"

#if defined(CONFIG_IDF_TARGET_ESP32S3)
#include "hal_esp32_target_esp32s3.h"
#else
#include "hal_esp32_target_esp32.h"
#endif

#ifndef BAUD
#define BAUD 115200UL
#endif

#define SPI_SD_FAST_HZ 1000000
#define SPI_TFT_FAST_HZ 26000000
#define SPI_SLOW_HZ 400000

static spi_device_handle_t tft_dev = NULL;
static spi_device_handle_t sd_dev = NULL;
static uint8_t spi2_ready = 0;
static uint8_t spi3_ready = 0;

static uint8_t *spi_ready_flag(spi_host_device_t host) {
    switch (host) {
        case SPI2_HOST:
            return &spi2_ready;
        case SPI3_HOST:
            return &spi3_ready;
        default:
            return &spi3_ready;
    }
}

static void spi_bus_setup(spi_host_device_t host, int sck, int miso, int mosi) {
    uint8_t *ready = spi_ready_flag(host);
    if (*ready) {
        return;
    }
    spi_bus_config_t buscfg = {
        .miso_io_num = miso,
        .mosi_io_num = mosi,
        .sclk_io_num = sck,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4096,
    };
    (void)spi_bus_initialize(host, &buscfg, SPI_DMA_CH_AUTO);
    *ready = 1;
}

static void spi_setup_tft(int clock_hz) {
    spi_bus_setup(HAL_ESP32_TFT_SPI_HOST, HAL_ESP32_SPI_SCK, HAL_ESP32_SPI_MISO, HAL_ESP32_SPI_MOSI);
    if (tft_dev) {
        (void)spi_bus_remove_device(tft_dev);
        tft_dev = NULL;
    }

    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = clock_hz,
        .mode = 0,
        .spics_io_num = -1,
        .queue_size = 1,
    };
    (void)spi_bus_add_device(HAL_ESP32_TFT_SPI_HOST, &devcfg, &tft_dev);
}

static void spi_setup_sd(int clock_hz) {
    spi_bus_setup(HAL_ESP32_SD_SPI_HOST, HAL_ESP32_SD_SPI_SCK, HAL_ESP32_SD_SPI_MISO, HAL_ESP32_SD_SPI_MOSI);
    if (sd_dev) {
        (void)spi_bus_remove_device(sd_dev);
        sd_dev = NULL;
    }

    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = clock_hz,
        .mode = 0,
        .spics_io_num = -1,
        .queue_size = 1,
    };
    (void)spi_bus_add_device(HAL_ESP32_SD_SPI_HOST, &devcfg, &sd_dev);
}

void hal_init(void) {
    hal_uart_init();
    hal_spi_tft_init();
    hal_spi_sd_init();

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

void hal_spi_tft_init(void) {
    spi_setup_tft(SPI_TFT_FAST_HZ);
}

void hal_spi_sd_init(void) {
    spi_setup_sd(SPI_SD_FAST_HZ);
}

void hal_spi_tft_set_speed_fast(void) {
    spi_setup_tft(SPI_TFT_FAST_HZ);
}

void hal_spi_sd_set_speed_fast(void) {
    spi_setup_sd(SPI_SD_FAST_HZ);
}

void hal_spi_sd_set_speed_very_slow(void) {
    spi_setup_sd(SPI_SLOW_HZ);
}

uint8_t hal_spi_sd_transfer(uint8_t data) {
    uint8_t rx = 0xFF;
    spi_transaction_t t = {0};
    t.length = 8;
    t.tx_buffer = &data;
    t.rx_buffer = &rx;
    (void)spi_device_transmit(sd_dev, &t);
    return rx;
}

void hal_spi_tft_write(uint8_t data) {
    spi_transaction_t t = {0};
    t.length = 8;
    t.tx_buffer = &data;
    (void)spi_device_transmit(tft_dev, &t);
}

void hal_spi_tft_write_buffer(const uint8_t *data, uint16_t len) {
    if (!data || len == 0) {
        return;
    }
    uint16_t remaining = len;
    uint16_t offset = 0;
    while (remaining) {
        uint16_t chunk = remaining > 4096 ? 4096 : remaining;
        spi_transaction_t t = {0};
        t.length = (size_t)chunk * 8u;
        t.tx_buffer = data + offset;
        (void)spi_device_transmit(tft_dev, &t);
        remaining = (uint16_t)(remaining - chunk);
        offset = (uint16_t)(offset + chunk);
    }
}

void hal_spi_sd_read_buffer(uint8_t *data, uint16_t len) {
    if (!data || len == 0) {
        return;
    }
    static uint8_t ff_buf[256];
    static uint8_t init = 0;
    if (!init) {
        for (uint16_t i = 0; i < sizeof(ff_buf); i++) {
            ff_buf[i] = 0xFF;
        }
        init = 1;
    }
    uint16_t remaining = len;
    uint16_t offset = 0;
    while (remaining) {
        uint16_t chunk = remaining > sizeof(ff_buf) ? (uint16_t)sizeof(ff_buf) : remaining;
        spi_transaction_t t = {0};
        t.length = (size_t)chunk * 8u;
        t.tx_buffer = ff_buf;
        t.rx_buffer = data + offset;
        (void)spi_device_transmit(sd_dev, &t);
        remaining = (uint16_t)(remaining - chunk);
        offset = (uint16_t)(offset + chunk);
    }
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
    return gpio_get_level(HAL_ESP32_SD_SPI_MISO) ? 1 : 0;
}
