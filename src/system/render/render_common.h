#pragma once

#include <stdint.h>

#include "drivers/card_reader/card_reader_common.h"

#if defined(ESP_PLATFORM)
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#endif

enum {
    RENDER_QUEUE_LENGTH = 4u,
    RENDER_SECONDARY_PRELOAD_QUEUE_LENGTH = 4u,
    RENDER_INTER_FRAME_DELAY_MS = 60u,
    RENDER_TASK_STACK_WORDS = 4096u,
    RENDER_SECONDARY_PRELOAD_TASK_STACK_WORDS = 4096u,
    RENDER_TASK_PRIORITY = 5u,
    RENDER_SECONDARY_PRELOAD_TASK_PRIORITY = 4u,
    RENDER_PRIMARY_CACHE_ENTRIES = 6u,
    RENDER_SECONDARY_CACHE_ENTRIES = 4u,
    RENDER_PRELOAD_LIST_CAPACITY = 2u,
    RENDER_NAME_CAPACITY = 13u,
    RENDER_BYTES_PER_PIXEL = 2u,
    RENDER_MAX_STREAM_CHUNK = 0xFFFFu,
    RENDER_TEXT_LINE_COUNT = 4u,
    RENDER_TEXT_LINE_CAPACITY = 32u,
    RENDER_TEXT_LINE_SPACING = 8u,
    RENDER_TEXT_FG_COLOR = 0xFFFFu,
    RENDER_TEXT_BG_COLOR = 0x0000u,
#if defined(ESP_PLATFORM)
    RENDER_EVENT_SECONDARY_PRELOAD_READY = BIT0,
#endif
};

typedef enum {
    RENDER_REQUEST_NONE = 0,
    RENDER_REQUEST_RAW565,
    RENDER_REQUEST_TEXT_SCREEN,
    RENDER_REQUEST_PRELOAD_SECONDARY,
    RENDER_REQUEST_RESET_SECONDARY_CACHE,
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
        struct {
            char names[RENDER_PRELOAD_LIST_CAPACITY][RENDER_NAME_CAPACITY];
            uint16_t width;
            uint16_t height;
            uint8_t count;
        } preload_secondary;
    } payload;
} render_request_t;

typedef struct {
    card_reader_state_t *reader;
    uint8_t *frame_buffer;
    uint32_t frame_buffer_capacity;
    uint8_t *secondary_preload_buffer;
    uint32_t secondary_preload_buffer_capacity;
    render_cache_entry_t primary_cache[RENDER_PRIMARY_CACHE_ENTRIES];
    render_cache_entry_t secondary_cache[RENDER_SECONDARY_CACHE_ENTRIES];
    uint8_t primary_cache_next;
    uint8_t secondary_cache_next;
#if defined(ESP_PLATFORM)
    EventGroupHandle_t sync_events;
    SemaphoreHandle_t cache_lock;
    QueueHandle_t request_q;
    QueueHandle_t secondary_preload_q;
    TaskHandle_t task_handle;
    TaskHandle_t secondary_preload_task_handle;
#endif
} render_state_t;

typedef struct {
    uint8_t *dst;
    uint32_t offset;
    uint32_t capacity;
} render_copy_ctx_t;
