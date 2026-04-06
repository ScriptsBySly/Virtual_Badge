#include "apps/animator/animator.h"

#include "drivers/card_reader/card_reader_api.h"
#include "drivers/display/display_api.h"
#include "hal/hal.h"

#include <string.h>

#if defined(ESP_PLATFORM)
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "esp_heap_caps.h"
#endif

#ifndef TFT_WIDTH
#define TFT_WIDTH 128u
#endif

#ifndef TFT_HEIGHT
#define TFT_HEIGHT 160u
#endif

enum {
    FRAME_INTERVAL_MS = 500u,
    BLINK_STEP_MS = 120u,
    TICK_MS = 20u,
    RAW565_CACHE_ENTRIES = 4,
};

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

typedef struct {
    char name[13];
    uint8_t *data;
    uint32_t size;
    uint8_t valid;
} raw565_cache_entry_t;

typedef struct {
    uint8_t *dst;
    uint32_t offset;
    uint32_t capacity;
} raw565_copy_ctx_t;

typedef enum {
    EVENT_NONE = 0,
    EVENT_HAPPY,
    EVENT_SADNEW,
    EVENT_MAD,
} event_type_t;

typedef struct {
    uint8_t active;
    uint8_t step;
    uint16_t remaining_ms;
    uint8_t mode;
} blink_state_t;

#if defined(ESP_PLATFORM)
typedef struct {
    char name[13];
    uint16_t width;
    uint16_t height;
} frame_request_t;

typedef struct {
    uint8_t *data;
    uint32_t size;
} frame_buffer_t;

typedef struct {
    card_reader_state_t *dev;
    QueueHandle_t request_q;
    QueueHandle_t ready_q;
    QueueHandle_t free_q;
    uint16_t width;
    uint16_t height;
} frame_tasks_ctx_t;

static QueueHandle_t g_frame_req_q = NULL;
static QueueHandle_t g_frame_ready_q = NULL;
static QueueHandle_t g_frame_free_q = NULL;
#endif

static raw565_cache_entry_t g_raw565_cache[RAW565_CACHE_ENTRIES];
static uint8_t g_raw565_cache_next = 0;
static uint16_t g_animator_rng_state = 0xACE1u;

static uint8_t animator_rng8(void)
{
    g_animator_rng_state = (uint16_t)((g_animator_rng_state >> 1) ^ (-(g_animator_rng_state & 1u) & 0xB400u));
    return (uint8_t)(g_animator_rng_state & 0xFFu);
}

static char *animator_append_str(char *p, const char *s)
{
    while (*s)
    {
        *p++ = *s++;
    }
    return p;
}

static char *animator_append_hex32(char *p, uint32_t v)
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

static char *animator_append_u8_dec(char *p, uint8_t v)
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

static void animator_build_raw_name(char *out, const char *base, const char *eyes, const char *mouth)
{
    out[0] = base[0];
    out[1] = base[1];
    out[2] = eyes[0];
    out[3] = eyes[1];
    out[4] = mouth[0];
    out[5] = mouth[1];
    out[6] = '.';
    out[7] = 'R';
    out[8] = 'A';
    out[9] = 'W';
    out[10] = '\0';
}

static void animator_build_event_name(char *out, const char *base, event_type_t ev)
{
    out[0] = base[0];
    out[1] = base[1];
    if (ev == EVENT_HAPPY)
    {
        out[2] = 'H'; out[3] = 'A'; out[4] = 'P'; out[5] = 'P'; out[6] = 'Y';
        out[7] = '.'; out[8] = 'R'; out[9] = 'A'; out[10] = 'W'; out[11] = '\0';
        return;
    }
    if (ev == EVENT_SADNEW)
    {
        out[2] = 'S'; out[3] = 'A'; out[4] = 'D'; out[5] = 'N'; out[6] = 'E'; out[7] = 'W';
        out[8] = '.'; out[9] = 'R'; out[10] = 'A'; out[11] = 'W'; out[12] = '\0';
        return;
    }

    out[2] = 'M'; out[3] = 'A'; out[4] = 'D';
    out[5] = '.'; out[6] = 'R'; out[7] = 'A'; out[8] = 'W'; out[9] = '\0';
}

static void animator_draw_error_screen(const char *name)
{
    display_fill_color(0x0000);
    display_draw_text(0, 0, "SD IMG FAIL", 0xFFFF, 0x0000);
    display_draw_text(0, 8, name, 0xFFFF, 0x0000);
}

static void animator_draw_sd_status_overlay(const card_reader_state_t *dev)
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

    p = animator_append_str(p, "SDHC:");
    p = animator_append_u8_dec(p, dev->status.sd_is_sdhc);
    p = animator_append_str(p, " FAT:");
    p = animator_append_u8_dec(p, dev->status.sd_fat32_ready);
    *p = '\0';
    display_draw_text(0, 8, buf, 0xFFFF, 0x0000);

    p = buf;
    p = animator_append_str(p, "C0:");
    p = animator_append_hex32(p, dev->regs.sd_last_cmd0_r1);
    *p = '\0';
    display_draw_text(0, 16, buf, 0xFFFF, 0x0000);

    p = buf;
    p = animator_append_str(p, "C8:");
    p = animator_append_hex32(p, dev->regs.sd_last_cmd8_r1);
    *p = '\0';
    display_draw_text(0, 24, buf, 0xFFFF, 0x0000);
}

static void animator_stream_bytes_sd(const uint8_t *data, uint16_t len, void *ctx)
{
    (void)ctx;
    display_stream_bytes(data, len);
}

static void animator_stream_bytes_cache(const uint8_t *data, uint16_t len, void *ctx)
{
    raw565_copy_ctx_t *copy = (raw565_copy_ctx_t *)ctx;
    uint32_t remaining = 0;
    uint16_t chunk = len;

    if (!copy || !copy->dst || copy->offset >= copy->capacity)
    {
        return;
    }

    remaining = copy->capacity - copy->offset;
    if ((uint32_t)chunk > remaining)
    {
        chunk = (uint16_t)remaining;
    }

    memcpy(copy->dst + copy->offset, data, chunk);
    copy->offset += chunk;
}

static raw565_cache_entry_t *animator_cache_find(const char *name, uint32_t expected)
{
    if (!name)
    {
        return 0;
    }

    for (uint8_t i = 0; i < RAW565_CACHE_ENTRIES; i++)
    {
        raw565_cache_entry_t *entry = &g_raw565_cache[i];
        if (!entry->valid || entry->size != expected)
        {
            continue;
        }
        if (strncmp(entry->name, name, sizeof(entry->name)) == 0)
        {
            return entry;
        }
    }
    return 0;
}

static raw565_cache_entry_t *animator_cache_store(const char *name, uint32_t size)
{
    raw565_cache_entry_t *entry = &g_raw565_cache[g_raw565_cache_next];
    g_raw565_cache_next = (uint8_t)((g_raw565_cache_next + 1u) % RAW565_CACHE_ENTRIES);
    entry->valid = 0;
    entry->size = size;

    if (!name)
    {
        entry->name[0] = '\0';
        return entry;
    }

    for (uint8_t i = 0; i < sizeof(entry->name) - 1; i++)
    {
        entry->name[i] = name[i];
        if (!name[i])
        {
            entry->valid = 1;
            return entry;
        }
    }

    entry->name[sizeof(entry->name) - 1] = '\0';
    entry->valid = 1;
    return entry;
}

static uint8_t animator_draw_raw565(card_reader_state_t *dev,
                                    const char *name,
                                    uint16_t width,
                                    uint16_t height)
{
    uint32_t expected = (uint32_t)width * (uint32_t)height * 2u;
    raw565_cache_entry_t *hit = animator_cache_find(name, expected);

    display_set_addr_window(width, height);
    if (hit)
    {
        if (expected <= 0xFFFFu)
        {
            display_stream_bytes(hit->data, (uint16_t)expected);
        }
        else
        {
            uint32_t offset = 0;
            while (offset < expected)
            {
                uint16_t chunk = (expected - offset) > 0xFFFFu ? 0xFFFFu : (uint16_t)(expected - offset);
                display_stream_bytes(hit->data + offset, chunk);
                offset += chunk;
            }
        }
        return 1;
    }

    return card_reader_file_read(dev, name, expected, animator_stream_bytes_sd, NULL);
}

static uint8_t animator_load_raw565(card_reader_state_t *dev,
                                    const char *name,
                                    uint16_t width,
                                    uint16_t height,
                                    uint8_t *dst,
                                    uint32_t capacity)
{
    uint32_t expected = (uint32_t)width * (uint32_t)height * 2u;
    raw565_cache_entry_t *hit = animator_cache_find(name, expected);
    raw565_copy_ctx_t sink = {
        .dst = dst,
        .offset = 0,
        .capacity = capacity,
    };

    if (hit)
    {
        if (dst && capacity >= expected)
        {
            memcpy(dst, hit->data, expected);
            return 1;
        }
        return 0;
    }

    if (dst && capacity >= expected)
    {
        uint8_t ok = card_reader_file_read(dev, name, expected, animator_stream_bytes_cache, &sink);
        if (ok)
        {
            raw565_cache_entry_t *entry = animator_cache_store(name, expected);
            entry->data = dst;
            entry->size = expected;
            entry->valid = 1;
        }
        return ok;
    }

    return 0;
}

#if defined(ESP_PLATFORM)
static void animator_frame_loader_task(void *arg)
{
    frame_tasks_ctx_t *ctx = (frame_tasks_ctx_t *)arg;

    for (;;)
    {
        frame_request_t req;
        frame_buffer_t *buf = NULL;
        if (xQueueReceive(ctx->request_q, &req, portMAX_DELAY) != pdTRUE)
        {
            continue;
        }
        if (xQueueReceive(ctx->free_q, &buf, portMAX_DELAY) != pdTRUE || !buf || !buf->data)
        {
            continue;
        }
        if (!animator_load_raw565(ctx->dev, req.name, req.width, req.height, buf->data, buf->size))
        {
            /* Keep the prior frame to avoid flashing black when SD reads fail. */
        }
        xQueueSend(ctx->ready_q, &buf, portMAX_DELAY);
    }
}

static void animator_frame_display_task(void *arg)
{
    frame_tasks_ctx_t *ctx = (frame_tasks_ctx_t *)arg;
    const uint32_t expected = (uint32_t)ctx->width * (uint32_t)ctx->height * 2u;

    for (;;)
    {
        frame_buffer_t *buf = NULL;
        if (xQueueReceive(ctx->ready_q, &buf, portMAX_DELAY) != pdTRUE || !buf || !buf->data)
        {
            continue;
        }
        display_set_addr_window(ctx->width, ctx->height);
        display_stream_bytes(buf->data, (uint16_t)expected);
        xQueueSend(ctx->free_q, &buf, portMAX_DELAY);
    }
}

static uint8_t animator_enqueue_frame(const char *name, uint16_t width, uint16_t height)
{
    frame_request_t req = {0};

    if (!g_frame_req_q || !name)
    {
        return 0;
    }

    for (uint8_t i = 0; i < sizeof(req.name) - 1 && name[i]; i++)
    {
        req.name[i] = name[i];
    }
    req.width = width;
    req.height = height;
    return xQueueSend(g_frame_req_q, &req, 0) == pdTRUE;
}
#endif

static uint8_t animator_draw_frame(card_reader_state_t *dev,
                                   const char *base,
                                   const char *eyes,
                                   const char *mouth)
{
    char name[11];
    uint8_t ok = 0;

    animator_build_raw_name(name, base, eyes, mouth);
#if defined(ESP_PLATFORM)
    if (g_frame_req_q)
    {
        return animator_enqueue_frame(name, TFT_WIDTH, TFT_HEIGHT);
    }
#endif
    ok = animator_draw_raw565(dev, name, TFT_WIDTH, TFT_HEIGHT);
    if (!ok)
    {
        animator_draw_error_screen(name);
    }
    return ok;
}

static uint8_t animator_draw_event_frame(card_reader_state_t *dev,
                                         const char *base,
                                         event_type_t ev)
{
    char name[13];
    uint8_t ok = 0;

    animator_build_event_name(name, base, ev);
#if defined(ESP_PLATFORM)
    if (g_frame_req_q)
    {
        return animator_enqueue_frame(name, TFT_WIDTH, TFT_HEIGHT);
    }
#endif
    ok = animator_draw_raw565(dev, name, TFT_WIDTH, TFT_HEIGHT);
    if (!ok)
    {
        animator_draw_error_screen(name);
    }
    return ok;
}

static void animator_blink_start(blink_state_t *b)
{
    b->active = 1;
    b->step = 0;
    b->mode = animator_rng8() % 4u;
    if (b->mode == 2)
    {
        b->remaining_ms = 1000u;
    }
    else if (b->mode == 3)
    {
        b->remaining_ms = 2000u + (uint16_t)(animator_rng8() % 7u) * 1000u;
    }
    else
    {
        b->remaining_ms = BLINK_STEP_MS;
    }
}

static const char *animator_blink_eyes_for_step(const blink_state_t *b)
{
    if (b->mode == 0)
    {
        return (b->step == 0) ? "EC" : "EO";
    }
    if (b->mode == 1)
    {
        if (b->step == 0) return "EM";
        if (b->step == 1) return "EC";
        return "EO";
    }
    if (b->mode == 2)
    {
        return (b->step == 0) ? "EM" : "EO";
    }
    return (b->step == 0) ? "EM" : "EO";
}

static uint8_t animator_blink_step_count(const blink_state_t *b)
{
    if (b->mode == 0) return 2;
    if (b->mode == 1) return 3;
    return 2;
}

static uint8_t animator_blink_tick(blink_state_t *b, uint16_t tick_ms)
{
    if (!b->active)
    {
        return 0;
    }
    if (b->remaining_ms > tick_ms)
    {
        b->remaining_ms -= tick_ms;
        return 0;
    }
    b->step++;
    if (b->step >= animator_blink_step_count(b))
    {
        b->active = 0;
        return 1;
    }
    b->remaining_ms = BLINK_STEP_MS;
    return 0;
}

static card_reader_state_t *animator_wait_for_sd_ready(void)
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
            p = animator_append_str(p, "TRY:");
            p = animator_append_u8_dec(p, sd_attempt);
            *p = '\0';
            display_draw_text(0, 8, buf, 0xFFFF, 0x0000);
            if (dev)
            {
                p = buf;
                p = animator_append_str(p, "C0:");
                p = animator_append_hex32(p, dev->regs.sd_last_cmd0_r1);
                *p = '\0';
                display_draw_text(0, 16, buf, 0xFFFF, 0x0000);

                p = buf;
                p = animator_append_str(p, "C8:");
                p = animator_append_hex32(p, dev->regs.sd_last_cmd8_r1);
                *p = '\0';
                display_draw_text(0, 24, buf, 0xFFFF, 0x0000);
            }
        }
        sd_attempt++;
        hal_delay_ms(500);
    }

    return dev;
}

uint8_t animator_app_task(void *ctx)
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

    hal_init();
    display_init();
    display_fill_color(0x0000);

    card_reader_state_t *dev = animator_wait_for_sd_ready();
    animator_draw_sd_status_overlay(dev);
    hal_delay_ms(2000);

#if defined(ESP_PLATFORM) && !TEST_SCREEN_DEBUG && !TEST_RGB_CYCLE && !TEST_LED_BLINK
#if 0
    const uint32_t frame_bytes = (uint32_t)TFT_WIDTH * (uint32_t)TFT_HEIGHT * 2u;
    frame_buffer_t buffers[2] = {0};
    for (uint8_t i = 0; i < 2; i++)
    {
        buffers[i].size = frame_bytes;
        buffers[i].data = (uint8_t *)heap_caps_malloc(frame_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!buffers[i].data)
        {
            buffers[i].data = (uint8_t *)malloc(frame_bytes);
        }
    }

    g_frame_req_q = xQueueCreate(4, sizeof(frame_request_t));
    g_frame_ready_q = xQueueCreate(2, sizeof(frame_buffer_t *));
    g_frame_free_q = xQueueCreate(2, sizeof(frame_buffer_t *));

    if (g_frame_req_q && g_frame_ready_q && g_frame_free_q)
    {
        for (uint8_t i = 0; i < 2; i++)
        {
            if (buffers[i].data)
            {
                frame_buffer_t *buf = &buffers[i];
                xQueueSend(g_frame_free_q, &buf, 0);
            }
        }
        static frame_tasks_ctx_t frame_ctx;
        frame_ctx.dev = dev;
        frame_ctx.request_q = g_frame_req_q;
        frame_ctx.ready_q = g_frame_ready_q;
        frame_ctx.free_q = g_frame_free_q;
        frame_ctx.width = TFT_WIDTH;
        frame_ctx.height = TFT_HEIGHT;
        xTaskCreate(animator_frame_loader_task, "frame_loader", 4096, &frame_ctx, 5, NULL);
        xTaskCreate(animator_frame_display_task, "frame_display", 4096, &frame_ctx, 5, NULL);
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

#if !TEST_SCREEN_DEBUG
    {
        const char *bases[2] = {"HU", "HD"};
        uint8_t base_index = 0;
        uint16_t frame_timer = FRAME_INTERVAL_MS;
        uint16_t blink_timer = 4u * FRAME_INTERVAL_MS;
        blink_state_t blink = {0};
        const char *base = bases[base_index];
        const char *eyes = "EO";
        event_type_t event = EVENT_NONE;
        uint16_t event_timer = (uint16_t)(5000u + (uint16_t)(animator_rng8() % 6u) * 1000u);
        uint16_t event_remaining = 0;

        animator_draw_frame(dev, base, eyes, "MC");
        while (1)
        {
            if (dev)
            {
                if (frame_timer <= TICK_MS)
                {
                    frame_timer = FRAME_INTERVAL_MS;
                    base_index ^= 1u;
                    base = bases[base_index];
                    if (event != EVENT_NONE)
                    {
                        animator_draw_event_frame(dev, base, event);
                    }
                    else
                    {
                        eyes = blink.active ? animator_blink_eyes_for_step(&blink) : "EO";
                        animator_draw_frame(dev, base, eyes, "MC");
                    }
                }
                else
                {
                    frame_timer -= TICK_MS;
                }

                if (event == EVENT_NONE)
                {
                    if (event_timer <= TICK_MS)
                    {
                        event = (event_type_t)(1u + (animator_rng8() % 3u));
                        event_remaining = 3000u;
                        animator_draw_event_frame(dev, base, event);
                    }
                    else
                    {
                        event_timer -= TICK_MS;
                    }
                }
                else
                {
                    if (event_remaining <= TICK_MS)
                    {
                        event = EVENT_NONE;
                        event_timer = (uint16_t)(5000u + (uint16_t)(animator_rng8() % 6u) * 1000u);
                        blink_timer = 4u * FRAME_INTERVAL_MS;
                        eyes = "EO";
                        animator_draw_frame(dev, base, eyes, "MC");
                    }
                    else
                    {
                        event_remaining -= TICK_MS;
                    }
                }

                if (event == EVENT_NONE)
                {
                    if (!blink.active)
                    {
                        if (blink_timer <= TICK_MS)
                        {
                            animator_blink_start(&blink);
                            eyes = animator_blink_eyes_for_step(&blink);
                            animator_draw_frame(dev, base, eyes, "MC");
                            blink_timer = (4u + (animator_rng8() % 8u)) * FRAME_INTERVAL_MS;
                        }
                        else
                        {
                            blink_timer -= TICK_MS;
                        }
                    }
                    else if (animator_blink_tick(&blink, TICK_MS))
                    {
                        eyes = "EO";
                        animator_draw_frame(dev, base, eyes, "MC");
                    }
                    else
                    {
                        eyes = animator_blink_eyes_for_step(&blink);
                    }
                }
            }

            hal_delay_ms(TICK_MS);
        }
    }
#endif

    return 1;
}
