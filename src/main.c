#include "drivers/card_reader/card_reader_api.h"
#include "apps/animator.h"
#include "drivers/display.h"
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

#define FRAME_INTERVAL_MS 500u
#define BLINK_STEP_MS 120u
#define TICK_MS 20u

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

static uint16_t rng_state = 0xACE1u;

static uint8_t rng8(void) {
    rng_state = (uint16_t)((rng_state >> 1) ^ (-(rng_state & 1u) & 0xB400u));
    return (uint8_t)(rng_state & 0xFFu);
}

static char *append_str(char *p, const char *s) {
    while (*s) {
        *p++ = *s++;
    }
    return p;
}

static char *append_hex32(char *p, uint32_t v) {
    static const char hex[] = "0123456789ABCDEF";
    *p++ = '0';
    *p++ = 'x';
    for (int8_t i = 7; i >= 0; i--) {
        uint8_t nib = (uint8_t)((v >> (uint8_t)(i * 4)) & 0xFu);
        *p++ = hex[nib];
    }
    return p;
}

static char *append_u8_dec(char *p, uint8_t v) {
    if (v >= 100) {
        *p++ = (char)('0' + (v / 100));
        v = (uint8_t)(v % 100);
        *p++ = (char)('0' + (v / 10));
        *p++ = (char)('0' + (v % 10));
        return p;
    }
    if (v >= 10) {
        *p++ = (char)('0' + (v / 10));
        *p++ = (char)('0' + (v % 10));
        return p;
    }
    *p++ = (char)('0' + v);
    return p;
}

static void build_raw_name(char *out, const char *base, const char *eyes, const char *mouth) {
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


static void draw_error_screen(const char *name) {
    display_fill_color(0x0000);
    display_draw_text(0, 0, "SD IMG FAIL", 0xFFFF, 0x0000);
    display_draw_text(0, 8, name, 0xFFFF, 0x0000);
}

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

static void frame_loader_task(void *arg) {
    frame_tasks_ctx_t *ctx = (frame_tasks_ctx_t *)arg;
    const uint32_t expected = (uint32_t)ctx->width * (uint32_t)ctx->height * 2u;
    for (;;) {
        frame_request_t req;
        frame_buffer_t *buf = NULL;
        if (xQueueReceive(ctx->request_q, &req, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        if (xQueueReceive(ctx->free_q, &buf, portMAX_DELAY) != pdTRUE || !buf || !buf->data) {
            continue;
        }
        uint8_t ok = animator_load_raw565(ctx->dev, req.name, req.width, req.height, buf->data, buf->size);
        if (!ok) {
            /* Keep previous frame in the buffer to avoid flashing black. */
        }
        xQueueSend(ctx->ready_q, &buf, portMAX_DELAY);
    }
}

static void frame_display_task(void *arg) {
    frame_tasks_ctx_t *ctx = (frame_tasks_ctx_t *)arg;
    const uint32_t expected = (uint32_t)ctx->width * (uint32_t)ctx->height * 2u;
    for (;;) {
        frame_buffer_t *buf = NULL;
        if (xQueueReceive(ctx->ready_q, &buf, portMAX_DELAY) != pdTRUE || !buf || !buf->data) {
            continue;
        }
        display_set_addr_window(ctx->width, ctx->height);
        display_stream_bytes(buf->data, (uint16_t)expected);
        xQueueSend(ctx->free_q, &buf, portMAX_DELAY);
    }
}

static uint8_t enqueue_frame(const char *name, uint16_t width, uint16_t height) {
    if (!g_frame_req_q || !name) {
        return 0;
    }
    frame_request_t req = {0};
    for (uint8_t i = 0; i < sizeof(req.name) - 1 && name[i]; i++) {
        req.name[i] = name[i];
    }
    req.width = width;
    req.height = height;
    return xQueueSend(g_frame_req_q, &req, 0) == pdTRUE;
}
#endif

static uint8_t draw_frame(card_reader_state_t *dev,
                          const char *base,
                          const char *eyes,
                          const char *mouth) {
    char name[11];
    build_raw_name(name, base, eyes, mouth);
#if defined(ESP_PLATFORM)
    if (g_frame_req_q) {
        return enqueue_frame(name, TFT_WIDTH, TFT_HEIGHT);
    }
#endif
    uint8_t ok = animator_draw_raw565(dev, name, TFT_WIDTH, TFT_HEIGHT);
    if (!ok) {
        draw_error_screen(name);
    }
    return ok;
}

typedef enum {
    EVENT_NONE = 0,
    EVENT_HAPPY,
    EVENT_SADNEW,
    EVENT_MAD,
} event_type_t;

static void build_event_name(char *out, const char *base, event_type_t ev) {
    out[0] = base[0];
    out[1] = base[1];
    if (ev == EVENT_HAPPY) {
        out[2] = 'H'; out[3] = 'A'; out[4] = 'P'; out[5] = 'P'; out[6] = 'Y';
        out[7] = '.'; out[8] = 'R'; out[9] = 'A'; out[10] = 'W'; out[11] = '\0';
        return;
    }
    if (ev == EVENT_SADNEW) {
        out[2] = 'S'; out[3] = 'A'; out[4] = 'D'; out[5] = 'N'; out[6] = 'E'; out[7] = 'W';
        out[8] = '.'; out[9] = 'R'; out[10] = 'A'; out[11] = 'W'; out[12] = '\0';
        return;
    }
    // EVENT_MAD
    out[2] = 'M'; out[3] = 'A'; out[4] = 'D';
    out[5] = '.'; out[6] = 'R'; out[7] = 'A'; out[8] = 'W'; out[9] = '\0';
}


static uint8_t draw_event_frame(card_reader_state_t *dev,
                                const char *base,
                                event_type_t ev) {
    char name[13];
    build_event_name(name, base, ev);
#if defined(ESP_PLATFORM)
    if (g_frame_req_q) {
        return enqueue_frame(name, TFT_WIDTH, TFT_HEIGHT);
    }
#endif
    uint8_t ok = animator_draw_raw565(dev, name, TFT_WIDTH, TFT_HEIGHT);
    if (!ok) {
        draw_error_screen(name);
    }
    return ok;
}

typedef struct {
    uint8_t active;
    uint8_t step;
    uint16_t remaining_ms;
    uint8_t mode;
} blink_state_t;

static void blink_start(blink_state_t *b) {
    b->active = 1;
    b->step = 0;
    b->mode = rng8() % 4u;
    if (b->mode == 2) {
        b->remaining_ms = 1000u;
    } else if (b->mode == 3) {
        b->remaining_ms = 2000u + (uint16_t)(rng8() % 7u) * 1000u;
    } else {
        b->remaining_ms = BLINK_STEP_MS;
    }
}

static const char *blink_eyes_for_step(const blink_state_t *b) {
    if (b->mode == 0) {
        return (b->step == 0) ? "EC" : "EO";
    }
    if (b->mode == 1) {
        if (b->step == 0) return "EM";
        if (b->step == 1) return "EC";
        return "EO";
    }
    if (b->mode == 2) {
        return (b->step == 0) ? "EM" : "EO";
    }
    // mode 3
    return (b->step == 0) ? "EM" : "EO";
}

static uint8_t blink_step_count(const blink_state_t *b) {
    if (b->mode == 0) return 2;
    if (b->mode == 1) return 3;
    return 2;
}

static uint8_t blink_tick(blink_state_t *b, uint16_t tick_ms) {
    if (!b->active) return 0;
    if (b->remaining_ms > tick_ms) {
        b->remaining_ms -= tick_ms;
        return 0;
    }
    b->step++;
    if (b->step >= blink_step_count(b)) {
        b->active = 0;
        return 1;
    }
    b->remaining_ms = BLINK_STEP_MS;
    return 0;
}

static void app_run(void) {
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
    while (1) {
        gpio_set_level(BLINK_GPIO, 1);
        hal_delay_ms(250);
        gpio_set_level(BLINK_GPIO, 0);
        hal_delay_ms(250);
    }
#else
    while (1) {
        hal_delay_ms(250);
    }
#endif
#endif

    hal_init();
    display_init();
    display_fill_color(0x0000);
    card_reader_state_t *dev = card_reader_file_open();

#if defined(ESP_PLATFORM) && !TEST_SCREEN_DEBUG && !TEST_RGB_CYCLE && !TEST_LED_BLINK
    const uint32_t frame_bytes = (uint32_t)TFT_WIDTH * (uint32_t)TFT_HEIGHT * 2u;
    frame_buffer_t buffers[2] = {0};
    for (uint8_t i = 0; i < 2; i++) {
        buffers[i].size = frame_bytes;
        buffers[i].data = (uint8_t *)heap_caps_malloc(frame_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!buffers[i].data) {
            buffers[i].data = (uint8_t *)malloc(frame_bytes);
        }
    }

    g_frame_req_q = xQueueCreate(4, sizeof(frame_request_t));
    g_frame_ready_q = xQueueCreate(2, sizeof(frame_buffer_t *));
    g_frame_free_q = xQueueCreate(2, sizeof(frame_buffer_t *));

    if (g_frame_req_q && g_frame_ready_q && g_frame_free_q) {
        for (uint8_t i = 0; i < 2; i++) {
            if (buffers[i].data) {
                frame_buffer_t *buf = &buffers[i];
                xQueueSend(g_frame_free_q, &buf, 0);
            }
        }
        static frame_tasks_ctx_t ctx;
        ctx.dev = dev;
        ctx.request_q = g_frame_req_q;
        ctx.ready_q = g_frame_ready_q;
        ctx.free_q = g_frame_free_q;
        ctx.width = TFT_WIDTH;
        ctx.height = TFT_HEIGHT;
        xTaskCreate(frame_loader_task, "frame_loader", 4096, &ctx, 5, NULL);
        xTaskCreate(frame_display_task, "frame_display", 4096, &ctx, 5, NULL);
    }
#endif

#if TEST_SCREEN_DEBUG
    uint16_t refresh_timer = 0;
    while (1) {
        if (refresh_timer <= TICK_MS) {
            refresh_timer = 1000u;
            display_fill_color(0x0000);

            uint8_t line = 0;
            char buf[32];

            display_draw_text(0, (uint16_t)(line++ * 8u), "DEBUG", 0xFFFF, 0x0000);

            char *p = buf;
            p = append_str(p, dev ? "DEV:OK" : "DEV:NULL");
            *p = '\0';
            display_draw_text(0, (uint16_t)(line++ * 8u), buf, 0xFFFF, 0x0000);
        } else {
            refresh_timer -= TICK_MS;
        }

        hal_delay_ms(TICK_MS);
    }
#endif

#if TEST_RGB_CYCLE
    const uint16_t colors[] = {0xF800u, 0x07E0u, 0x001Fu, 0xFFFFu, 0x0000u};
    uint8_t color_index = 0;
    while (1) {
        display_fill_color(colors[color_index]);
        color_index = (uint8_t)((color_index + 1u) % (sizeof(colors) / sizeof(colors[0])));
        hal_delay_ms(500);
    }
#endif

#if !TEST_SCREEN_DEBUG
    const char *bases[2] = {"HU", "HD"};
    uint8_t base_index = 0;
    uint16_t frame_timer = FRAME_INTERVAL_MS;
    uint16_t blink_timer = 4u * FRAME_INTERVAL_MS;
    blink_state_t blink = {0};
    const char *base = bases[base_index];
    const char *eyes = "EO";
    draw_frame(dev, base, eyes, "MC");
    event_type_t event = EVENT_NONE;
    uint16_t event_timer = (uint16_t)(5000u + (uint16_t)(rng8() % 6u) * 1000u);
    uint16_t event_remaining = 0;

    while (1) {
        if (dev) {
            if (frame_timer <= TICK_MS) {
                frame_timer = FRAME_INTERVAL_MS;
                base_index ^= 1u;
                base = bases[base_index];
                if (event != EVENT_NONE) {
                    draw_event_frame(dev, base, event);
                } else {
                    eyes = blink.active ? blink_eyes_for_step(&blink) : "EO";
                    draw_frame(dev, base, eyes, "MC");
                }
            } else {
                frame_timer -= TICK_MS;
            }

            if (event == EVENT_NONE) {
                if (event_timer <= TICK_MS) {
                    event = (event_type_t)(1u + (rng8() % 3u));
                    event_remaining = 3000u;
                    draw_event_frame(dev, base, event);
                } else {
                    event_timer -= TICK_MS;
                }
            } else {
                if (event_remaining <= TICK_MS) {
                    event = EVENT_NONE;
                    event_timer = (uint16_t)(5000u + (uint16_t)(rng8() % 6u) * 1000u);
                    blink_timer = 4u * FRAME_INTERVAL_MS;
                    eyes = "EO";
                    draw_frame(dev, base, eyes, "MC");
                } else {
                    event_remaining -= TICK_MS;
                }
            }

            if (event == EVENT_NONE) {
                if (!blink.active) {
                    if (blink_timer <= TICK_MS) {
                        blink_start(&blink);
                        eyes = blink_eyes_for_step(&blink);
                        draw_frame(dev, base, eyes, "MC");
                        blink_timer = (4u + (rng8() % 8u)) * FRAME_INTERVAL_MS;
                    } else {
                        blink_timer -= TICK_MS;
                    }
                } else {
                    if (blink_tick(&blink, TICK_MS)) {
                        eyes = "EO";
                        draw_frame(dev, base, eyes, "MC");
                    } else {
                        eyes = blink_eyes_for_step(&blink);
                    }
                }
            }
        }

        hal_delay_ms(TICK_MS);
    }
#endif
}

#ifdef ESP_PLATFORM
void app_main(void) {
    app_run();
}
#else
int main(void) {
    app_run();
    return 0;
}
#endif
