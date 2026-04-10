#pragma once

#include <stdint.h>

#include "drivers/card_reader/card_reader_common.h"

#if defined(ESP_PLATFORM)
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#endif

enum {
    RENDER_QUEUE_LENGTH = 4u,
    RENDER_INTER_FRAME_DELAY_MS = 60u,
    RENDER_TASK_STACK_WORDS = 4096u,
    RENDER_TASK_PRIORITY = 5u,
    RENDER_PRIMARY_CACHE_ENTRIES = 4u,
    RENDER_SECONDARY_CACHE_ENTRIES = 4u,
    RENDER_NAME_CAPACITY = 13u,
    RENDER_BYTES_PER_PIXEL = 2u,
    RENDER_MAX_STREAM_CHUNK = 0xFFFFu,
    RENDER_TEXT_LINE_COUNT = 4u,
    RENDER_TEXT_LINE_CAPACITY = 32u,
    RENDER_TEXT_LINE_SPACING = 8u,
    RENDER_TEXT_FG_COLOR = 0xFFFFu,
    RENDER_TEXT_BG_COLOR = 0x0000u,
};

typedef enum {
    RENDER_REQUEST_NONE = 0,
    RENDER_REQUEST_RAW565,
    RENDER_REQUEST_TEXT_SCREEN,
} render_request_type_t;

typedef struct {
    char name[RENDER_NAME_CAPACITY];
    uint8_t *data;
    uint32_t size;
    uint8_t valid;
} render_cache_entry_t;

typedef struct {
    render_request_type_t type;
    union {
        struct {
            char name[RENDER_NAME_CAPACITY];
            uint16_t width;
            uint16_t height;
        } raw565;
        struct {
            char lines[RENDER_TEXT_LINE_COUNT][RENDER_TEXT_LINE_CAPACITY];
        } text_screen;
    } payload;
} render_request_t;

typedef struct {
    card_reader_state_t *reader;
    uint8_t *frame_buffer;
    uint32_t frame_buffer_capacity;
    render_cache_entry_t primary_cache[RENDER_PRIMARY_CACHE_ENTRIES];
    render_cache_entry_t secondary_cache[RENDER_SECONDARY_CACHE_ENTRIES];
    uint8_t primary_cache_next;
    uint8_t secondary_cache_next;
#if defined(ESP_PLATFORM)
    QueueHandle_t request_q;
    TaskHandle_t task_handle;
#endif
} render_state_t;

typedef struct {
    uint8_t *dst;
    uint32_t offset;
    uint32_t capacity;
} render_copy_ctx_t;
