#include "system/render/render_image.h"

#include "system/render/render_text.h"

#include "drivers/card_reader/card_reader_api.h"
#include "drivers/display/display_api.h"

#include <stdlib.h>
#include <string.h>

#if defined(ESP_PLATFORM)
#include "esp_heap_caps.h"
#endif

/************************************************
* render_stream_bytes_cache
* Copies streamed file bytes into the destination frame buffer.
* Parameters: data = source bytes, len = chunk length, ctx = copy context.
* Returns: void.
***************************************************/
static void render_stream_bytes_cache(const uint8_t *data, uint16_t len, void *ctx)
{
    render_copy_ctx_t *copy = (render_copy_ctx_t *)ctx;
    uint32_t remaining = 0;
    uint16_t chunk = len;

    /* Refuse to copy when the sink state is invalid or already full. */
    if (!copy || !copy->dst || copy->offset >= copy->capacity)
    {
        return;
    }

    /* Cap the incoming chunk so we never write past the destination buffer. */
    remaining = copy->capacity - copy->offset;
    if ((uint32_t)chunk > remaining)
    {
        chunk = (uint16_t)remaining;
    }

    /* Append the bytes at the current offset and advance the write cursor. */
    memcpy(copy->dst + copy->offset, data, chunk);
    copy->offset += chunk;
}

/************************************************
* render_display_bytes
* Streams a full frame buffer to the display in controller-sized chunks.
* Parameters: data = frame bytes, size = byte count.
* Returns: void.
***************************************************/
static void render_display_bytes(const uint8_t *data, uint32_t size)
{
    uint32_t offset = 0;

    /* The display stream API takes bounded chunks, so split large frames as we send them. */
    while (offset < size)
    {
        uint16_t chunk = (size - offset) > RENDER_MAX_STREAM_CHUNK
            ? RENDER_MAX_STREAM_CHUNK
            : (uint16_t)(size - offset);
        display_stream_bytes(data + offset, chunk);
        offset += chunk;
    }
}

/************************************************
* render_cache_find
* Searches the image cache for a matching file name and size.
* Parameters: state = render state, name = file name, expected_size = byte count.
* Returns: matching cache entry, or NULL when not found.
***************************************************/
static render_cache_entry_t *render_cache_find(render_state_t *state, const char *name, uint32_t expected_size)
{
    uint8_t i = 0;

    /* A cache lookup only makes sense when both state and a file name are present. */
    if (!state || !name)
    {
        return 0;
    }

    /* Match on both size and name so different assets cannot alias the same slot. */
    for (i = 0; i < RENDER_CACHE_ENTRIES; i++)
    {
        render_cache_entry_t *entry = &state->cache[i];
        if (!entry->valid || entry->size != expected_size)
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

/************************************************
* render_cache_prepare_entry
* Ensures a cache slot owns storage for the requested image metadata.
* Parameters: entry = cache slot, name = file name, expected_size = byte count.
* Returns: 1 on success, 0 on failure.
***************************************************/
static uint8_t render_cache_prepare_entry(render_cache_entry_t *entry,
                                          const char *name,
                                          uint32_t expected_size)
{
    uint8_t i = 0;

    /* We need both a destination slot and a file name to prepare cache metadata. */
    if (!entry || !name)
    {
        return 0;
    }

    /* Allocate or resize backing storage whenever the slot cannot hold this image size. */
    if (!entry->data || entry->size != expected_size)
    {
#if defined(ESP_PLATFORM)
        /* Prefer PSRAM for frame assets, then fall back to normal heap if unavailable. */
        void *new_data = heap_caps_malloc(expected_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!new_data)
        {
            new_data = malloc(expected_size);
        }
#else
        void *new_data = malloc(expected_size);
#endif
        if (!new_data)
        {
            entry->valid = 0;
            return 0;
        }
        /* Release the old buffer only after we have a replacement ready. */
        if (entry->data)
        {
            free(entry->data);
        }
        entry->data = (uint8_t *)new_data;
        entry->size = expected_size;
    }

    /* Copy the file name into the fixed-size slot and leave room for a terminator. */
    for (i = 0; i < sizeof(entry->name) - 1u; i++)
    {
        entry->name[i] = name[i];
        if (!name[i])
        {
            break;
        }
    }
    entry->name[sizeof(entry->name) - 1u] = '\0';
    entry->valid = 1;
    return 1;
}

/************************************************
* render_cache_store
* Stores a rendered image buffer into the rotating in-memory cache.
* Parameters: state = render state, name = file name, data = source bytes, size = byte count.
* Returns: void.
***************************************************/
static void render_cache_store(render_state_t *state, const char *name, const uint8_t *data, uint32_t size)
{
    render_cache_entry_t *entry = 0;

    /* Without render state there is nowhere to cache the finished image. */
    if (!state)
    {
        return;
    }

    /* Use a simple rotating slot index so cache replacement stays predictable and cheap. */
    entry = &state->cache[state->cache_next];
    state->cache_next = (uint8_t)((state->cache_next + 1u) % RENDER_CACHE_ENTRIES);

    /* Skip caching if we cannot prepare the target slot. */
    if (!data || !render_cache_prepare_entry(entry, name, size))
    {
        return;
    }

    /* Copy the just-loaded frame into the cache for faster reuse on the next request. */
    memcpy(entry->data, data, size);
}

/************************************************
* render_ensure_frame_buffer
* Allocates or grows the shared frame buffer used for image requests.
* Parameters: state = render state, required_size = minimum byte capacity.
* Returns: 1 on success, 0 on failure.
***************************************************/
static uint8_t render_ensure_frame_buffer(render_state_t *state, uint32_t required_size)
{
    /* A valid render state is required before we can manage the shared frame buffer. */
    if (!state)
    {
        return 0;
    }

    /* Reuse the existing buffer when it is already large enough for this request. */
    if (state->frame_buffer && state->frame_buffer_capacity >= required_size)
    {
        return 1;
    }

    /* Drop the old buffer before allocating a larger replacement. */
    if (state->frame_buffer)
    {
        free(state->frame_buffer);
        state->frame_buffer = 0;
        state->frame_buffer_capacity = 0;
    }

#if defined(ESP_PLATFORM)
    /* Prefer PSRAM for large frame buffers, then fall back to standard heap. */
    state->frame_buffer = (uint8_t *)heap_caps_malloc(required_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!state->frame_buffer)
    {
        state->frame_buffer = (uint8_t *)malloc(required_size);
    }
#else
    state->frame_buffer = (uint8_t *)malloc(required_size);
#endif
    if (!state->frame_buffer)
    {
        return 0;
    }

    /* Record the new usable capacity so later requests can reuse the allocation. */
    state->frame_buffer_capacity = required_size;
    return 1;
}

/************************************************
* render_load_raw565
* Loads a RAW565 image into the provided buffer, using cache when available.
* Parameters: state = render state, name = file name, width = image width,
*             height = image height, dst = output buffer, capacity = buffer size.
* Returns: 1 on success, 0 on failure.
***************************************************/
static uint8_t render_load_raw565(render_state_t *state,
                                  const char *name,
                                  uint16_t width,
                                  uint16_t height,
                                  uint8_t *dst,
                                  uint32_t capacity)
{
    const uint32_t expected_size = (uint32_t)width * (uint32_t)height * RENDER_BYTES_PER_PIXEL;
    render_cache_entry_t *hit = render_cache_find(state, name, expected_size);
    render_copy_ctx_t sink = {
        .dst = dst,
        .offset = 0,
        .capacity = capacity,
    };

    /* The image path needs a bound reader, an output buffer, and enough room for the frame. */
    if (!state || !state->reader || !dst || capacity < expected_size)
    {
        return 0;
    }

    /* Serve cache hits immediately to avoid touching the SD card again. */
    if (hit)
    {
        memcpy(dst, hit->data, expected_size);
        return 1;
    }

    /* Stream the file from storage into the destination buffer when no cache hit exists. */
    if (!card_reader_file_read(state->reader, name, expected_size, render_stream_bytes_cache, &sink))
    {
        return 0;
    }

    /* Store the freshly loaded image so repeated frames can be reused from memory. */
    render_cache_store(state, name, dst, expected_size);
    return 1;
}

/************************************************
* render_image_queue_request
* Populates a render request with RAW565 image parameters.
* Parameters: request = output request, name = file name, width = image width,
*             height = image height.
* Returns: 1 on success, 0 on failure.
***************************************************/
uint8_t render_image_queue_request(render_request_t *request,
                                   const char *name,
                                   uint16_t width,
                                   uint16_t height)
{
    uint8_t i = 0;

    /* The request object and file name are mandatory for an image request. */
    if (!request || !name)
    {
        return 0;
    }

    /* Mark the request so render_core dispatches it to the image path. */
    request->type = RENDER_REQUEST_RAW565;
    /* Copy the name into the request payload and leave room for a terminator. */
    for (i = 0; i < sizeof(request->payload.raw565.name) - 1u && name[i]; i++)
    {
        request->payload.raw565.name[i] = name[i];
    }
    request->payload.raw565.width = width;
    request->payload.raw565.height = height;
    return 1;
}

/************************************************
* render_image_process_request
* Loads and displays a queued RAW565 request through the image pipeline.
* Parameters: state = render state, request = image request to process.
* Returns: 1 on success, 0 on failure.
***************************************************/
uint8_t render_image_process_request(render_state_t *state, const render_request_t *request)
{
    const uint32_t expected_size =
        (uint32_t)request->payload.raw565.width * (uint32_t)request->payload.raw565.height * RENDER_BYTES_PER_PIXEL;

    /* Image rendering requires state, a request, and a bound reader for SD-backed assets. */
    if (!state || !request || !state->reader)
    {
        return 0;
    }

    /* Ensure the shared scratch buffer can hold the requested frame size. */
    if (!render_ensure_frame_buffer(state, expected_size))
    {
        return 0;
    }

    /* Load the frame into memory before touching the display. */
    if (!render_load_raw565(state,
                            request->payload.raw565.name,
                            request->payload.raw565.width,
                            request->payload.raw565.height,
                            state->frame_buffer,
                            state->frame_buffer_capacity))
    {
        /* Fall back to a text error screen when the image file cannot be loaded. */
        return render_text_show_image_load_error(request->payload.raw565.name);
    }

    /* Point the controller at the target frame area and stream the decoded bytes. */
    display_set_addr_window(request->payload.raw565.width, request->payload.raw565.height);
    render_display_bytes(state->frame_buffer, expected_size);
    return 1;
}
