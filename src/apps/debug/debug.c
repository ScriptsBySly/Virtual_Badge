#include "apps/debug/debug.h"

#include "drivers/card_reader/card_reader_api.h"
#include "drivers/display/display_api.h"
#include "hal/hal.h"

#if defined(ESP_PLATFORM)
#include "driver/gpio.h"
#endif

#ifndef TEST_LED_BLINK
#define TEST_LED_BLINK 0
#endif

#ifndef BLINK_GPIO
#define BLINK_GPIO 2
#endif

#ifndef TEST_RGB_CYCLE
#define TEST_RGB_CYCLE 0
#endif

#ifndef TEST_SCREEN_DEBUG
#define TEST_SCREEN_DEBUG 0
#endif

static char *debug_append_str(char *p, const char *s)
{
    while (*s)
    {
        *p++ = *s++;
    }
    return p;
}

static char *debug_append_hex32(char *p, uint32_t v)
{
    static const char hex[] = "0123456789ABCDEF";
    *p++ = '0';
    *p++ = 'x';
    for (int8_t i = 7; i >= 0; i--)
    {
        uint8_t nib = (uint8_t)((v >> (uint8_t)(i * 4)) & 0xFu);
        *p++ = hex[nib];
    }
    return p;
}

static char *debug_append_u8_dec(char *p, uint8_t v)
{
    if (v >= 100)
    {
        *p++ = (char)('0' + (v / 100));
        v = (uint8_t)(v % 100);
        *p++ = (char)('0' + (v / 10));
        *p++ = (char)('0' + (v % 10));
        return p;
    }
    if (v >= 10)
    {
        *p++ = (char)('0' + (v / 10));
        *p++ = (char)('0' + (v % 10));
        return p;
    }
    *p++ = (char)('0' + v);
    return p;
}

static void debug_draw_sd_status_overlay(const card_reader_state_t *dev)
{
    char buf[32];
    char *p = buf;

    display_fill_color(0x0000);
    display_draw_text(0, 0, "SD STATUS", 0xFFFF, 0x0000);
    if (!dev)
    {
        display_draw_text(0, 8, "SD:FAIL", 0xFFFF, 0x0000);
        return;
    }

    p = debug_append_str(p, "SDHC:");
    p = debug_append_u8_dec(p, dev->status.sd_is_sdhc);
    p = debug_append_str(p, " FAT:");
    p = debug_append_u8_dec(p, dev->status.sd_fat32_ready);
    *p = '\0';
    display_draw_text(0, 8, buf, 0xFFFF, 0x0000);

    p = buf;
    p = debug_append_str(p, "C0:");
    p = debug_append_hex32(p, dev->regs.sd_last_cmd0_r1);
    *p = '\0';
    display_draw_text(0, 16, buf, 0xFFFF, 0x0000);

    p = buf;
    p = debug_append_str(p, "C8:");
    p = debug_append_hex32(p, dev->regs.sd_last_cmd8_r1);
    *p = '\0';
    display_draw_text(0, 24, buf, 0xFFFF, 0x0000);
}

static card_reader_state_t *debug_wait_for_sd_ready(void)
{
    card_reader_state_t *dev = NULL;
    uint8_t sd_attempt = 0;

    while (!dev || !dev->status.sd_fat32_ready)
    {
        if (dev)
        {
            card_reader_file_close(dev);
            dev = NULL;
        }

        hal_spi_sd_init();
        hal_spi_sd_set_speed_very_slow();
        hal_sd_cs_high();
        hal_delay_ms(50);

        dev = card_reader_file_open();
        if (dev && dev->status.sd_fat32_ready)
        {
            break;
        }

        display_fill_color(0x0000);
        display_draw_text(0, 0, "WAIT SD", 0xFFFF, 0x0000);
        {
            char buf[32];
            char *p = buf;
            p = debug_append_str(p, "TRY:");
            p = debug_append_u8_dec(p, sd_attempt);
            *p = '\0';
            display_draw_text(0, 8, buf, 0xFFFF, 0x0000);
            if (dev)
            {
                p = buf;
                p = debug_append_str(p, "C0:");
                p = debug_append_hex32(p, dev->regs.sd_last_cmd0_r1);
                *p = '\0';
                display_draw_text(0, 16, buf, 0xFFFF, 0x0000);

                p = buf;
                p = debug_append_str(p, "C8:");
                p = debug_append_hex32(p, dev->regs.sd_last_cmd8_r1);
                *p = '\0';
                display_draw_text(0, 24, buf, 0xFFFF, 0x0000);
            }
        }
        sd_attempt++;
        hal_delay_ms(500);
    }

    return dev;
}

uint8_t debug_app_task(void *ctx)
{
    (void)ctx;

#if TEST_LED_BLINK
#if defined(ESP_PLATFORM)
    gpio_config_t io_cfg = {
        .pin_bit_mask = (1ULL << BLINK_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_cfg);
    while (1)
    {
        gpio_set_level(BLINK_GPIO, 1);
        hal_delay_ms(250);
        gpio_set_level(BLINK_GPIO, 0);
        hal_delay_ms(250);
    }
#else
    while (1)
    {
        hal_delay_ms(250);
    }
#endif
#endif

#if TEST_RGB_CYCLE
    {
        const uint16_t colors[] = {0xF800u, 0x07E0u, 0x001Fu, 0xFFFFu, 0x0000u};
        uint8_t color_index = 0;
        while (1)
        {
            display_fill_color(colors[color_index]);
            color_index = (uint8_t)((color_index + 1u) % (sizeof(colors) / sizeof(colors[0])));
            hal_delay_ms(500);
        }
    }
#endif

#if TEST_SCREEN_DEBUG
    {
        card_reader_state_t *dev = debug_wait_for_sd_ready();
        while (1)
        {
            debug_draw_sd_status_overlay(dev);
            hal_delay_ms(1000);
        }
    }
#endif

    return 1;
}
