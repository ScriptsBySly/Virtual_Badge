#include "apps/animator.h"

#include "drivers/card_reader/card_reader_api.h"
#include "drivers/display.h"
#include "hal/hal.h"

#include <stdlib.h>
#include <string.h>

#if defined(ESP_PLATFORM)
#define IMAGE_CACHE_ENABLE 1
#define IMAGE_CACHE_SLOTS 3
#else
#define IMAGE_CACHE_ENABLE 0
#define IMAGE_CACHE_SLOTS 0
#endif

typedef struct {
    uint8_t valid;
    char name[13];
    uint8_t *data;
    uint32_t size;
    uint32_t last_use;
} image_cache_entry_t;

static image_cache_entry_t image_cache[IMAGE_CACHE_SLOTS];
static uint32_t image_cache_tick = 0;

#if IMAGE_CACHE_ENABLE
static image_cache_entry_t *cache_find(const char *name) {
    for (uint8_t i = 0; i < IMAGE_CACHE_SLOTS; i++) {
        if (image_cache[i].valid && strcmp(image_cache[i].name, name) == 0) {
            image_cache[i].last_use = ++image_cache_tick;
            return &image_cache[i];
        }
    }
    return NULL;
}

static image_cache_entry_t *cache_reserve(const char *name, uint32_t size) {
    image_cache_entry_t *slot = NULL;
    for (uint8_t i = 0; i < IMAGE_CACHE_SLOTS; i++) {
        if (!image_cache[i].valid) {
            slot = &image_cache[i];
            break;
        }
    }
    if (!slot) {
        uint32_t best = image_cache[0].last_use;
        uint8_t best_i = 0;
        for (uint8_t i = 1; i < IMAGE_CACHE_SLOTS; i++) {
            if (image_cache[i].last_use < best) {
                best = image_cache[i].last_use;
                best_i = i;
            }
        }
        slot = &image_cache[best_i];
    }
    if (slot->valid && slot->data) {
        free(slot->data);
    }
    slot->data = (uint8_t *)malloc(size);
    if (!slot->data) {
        slot->valid = 0;
        return NULL;
    }
    strncpy(slot->name, name, sizeof(slot->name) - 1);
    slot->name[sizeof(slot->name) - 1] = '\0';
    slot->size = size;
    slot->valid = 1;
    slot->last_use = ++image_cache_tick;
    return slot;
}
#endif

static void tft_stream_bytes_sd(const uint8_t *data, uint16_t len, void *ctx) {
    (void)ctx;
    hal_sd_cs_high();
    display_stream_bytes(data, len);
}

typedef struct {
    uint8_t *dst;
    uint32_t offset;
    uint32_t size;
    uint8_t stream;
} cache_sink_t;

static void tft_stream_bytes_cache(const uint8_t *data, uint16_t len, void *ctx) {
    cache_sink_t *sink = (cache_sink_t *)ctx;
    if (sink->dst && sink->offset + len <= sink->size) {
        memcpy(sink->dst + sink->offset, data, len);
    }
    sink->offset += len;
    if (sink->stream) {
        hal_sd_cs_high();
        display_stream_bytes(data, len);
    }
}

uint8_t animator_draw_raw565(card_reader_state_t *dev,
                             const char *name,
                             uint16_t width,
                             uint16_t height) {
    if (!dev) {
        return 0;
    }
    uint32_t expected = (uint32_t)width * (uint32_t)height * 2u;
    display_set_addr_window(width, height);
#if IMAGE_CACHE_ENABLE
    image_cache_entry_t *hit = cache_find(name);
    if (hit && hit->data && hit->size == expected) {
        hal_sd_cs_high();
        if (expected <= 65535u) {
            display_stream_bytes(hit->data, (uint16_t)expected);
        } else {
            uint32_t remaining = expected;
            uint32_t offset = 0;
            while (remaining) {
                uint16_t chunk = remaining > 4096 ? 4096 : (uint16_t)remaining;
                display_stream_bytes(hit->data + offset, chunk);
                remaining -= chunk;
                offset += chunk;
            }
        }
        return 1;
    }

    image_cache_entry_t *slot = cache_reserve(name, expected);
    if (slot && slot->data) {
        cache_sink_t sink = {
            .dst = slot->data,
            .offset = 0,
            .size = slot->size,
            .stream = 1,
        };
        uint8_t ok = card_reader_file_read(dev, name, expected, tft_stream_bytes_cache, &sink);
        if (!ok) {
            free(slot->data);
            slot->data = NULL;
            slot->valid = 0;
        }
        return ok;
    }
#endif
    return card_reader_file_read(dev, name, expected, tft_stream_bytes_sd, NULL);
}
