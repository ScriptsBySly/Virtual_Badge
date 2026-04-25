#include "system/render/render_core.h"

#include "system/render/render_common.h"
#include "system/render/render_cache.h"
#include "system/render/render_image.h"
#include "system/render/render_text.h"

#if defined(ESP_PLATFORM)
#include "sdkconfig.h"
#endif

#include <stdio.h>
#include <string.h>

#if defined(ESP_PLATFORM)
#include "esp_heap_caps.h"
#endif

static render_state_t g_render_state;
static uint8_t g_render_initialized = 0;

#if defined(ESP_PLATFORM)
static void render_core_set_secondary_preload_ready(uint8_t ready)
{
    if (!g_render_state.sync_events)
    {
        return;
    }

    if (ready)
    {
        (void)xEventGroupSetBits(g_render_state.sync_events, RENDER_EVENT_SECONDARY_PRELOAD_READY);
        printf("SR:1\n");
    }
    else
    {
        (void)xEventGroupClearBits(g_render_state.sync_events, RENDER_EVENT_SECONDARY_PRELOAD_READY);
        printf("SR:0\n");
    }
}
#endif

/************************************************
* render_request_debug_name
* Returns a short debug label for the current render request.
* Parameters: request = render request to describe.
* Returns: request name string for logs.
***************************************************/
static const char *render_request_debug_name(const render_request_t *request)
{
    if (!request)
    {
        return "?";
    }

    if (request->type == RENDER_REQUEST_RAW565)
    {
        return request->payload.raw565.name;
    }

    if (request->type == RENDER_REQUEST_TEXT_SCREEN)
    {
        return "TXT";
    }

    if (request->type == RENDER_REQUEST_PRELOAD_SECONDARY)
    {
        return "P";
    }

    if (request->type == RENDER_REQUEST_RESET_SECONDARY_CACHE)
    {
        return "RC";
    }

    return "?";
}

/************************************************
* render_process_request
* Routes a queued render request to the appropriate specialized handler.
* Parameters: request = render request to process.
* Returns: 1 on success, 0 on failure.
***************************************************/
static uint8_t render_process_request(const render_request_t *request)
{
    /* Ignore missing requests rather than dereferencing a NULL pointer. */
    if (!request)
    {
        return 0;
    }

    /* Text screens are routed to the text renderer without touching image state. */
    if (request->type == RENDER_REQUEST_TEXT_SCREEN)
    {
        return render_text_process_request(request);
    }

    /* RAW565 requests are routed to the image pipeline, which owns caching and SD reads. */
    if (request->type == RENDER_REQUEST_RAW565)
    {
        return render_image_process_request(&g_render_state, request);
    }

    /* Secondary-cache preloads are executed by render so app tasks never block on SD reads. */
    if (request->type == RENDER_REQUEST_PRELOAD_SECONDARY)
    {
        uint8_t i = 0;
        uint8_t ok = 1;

        for (i = 0; i < request->payload.preload_secondary.count; i++)
        {
            ok = (uint8_t)(ok && render_image_preload_secondary(&g_render_state,
                                                                request->payload.preload_secondary.names[i],
                                                                request->payload.preload_secondary.width,
                                                                request->payload.preload_secondary.height));
        }
#if defined(ESP_PLATFORM)
        render_core_set_secondary_preload_ready(ok);
#endif
        printf("PB:%u\n", ok);
        return ok;
    }

    /* Secondary-cache clears are also handled inside render so cache ownership stays centralized. */
    if (request->type == RENDER_REQUEST_RESET_SECONDARY_CACHE)
    {
        render_cache_reset_secondary(&g_render_state);
#if defined(ESP_PLATFORM)
        render_core_set_secondary_preload_ready(0);
#endif
        return 1;
    }

    /* Reject unknown request kinds so the caller gets a clear failure signal. */
    return 0;
}

/************************************************
* render_request_needs_pacing
* Reports whether a request should incur the inter-frame pacing delay.
* Parameters: request = render request to classify.
* Returns: 1 when pacing should be applied, 0 otherwise.
***************************************************/
static uint8_t render_request_needs_pacing(const render_request_t *request)
{
    if (!request)
    {
        return 0;
    }

    return request->type == RENDER_REQUEST_RAW565;
}

/************************************************
* render_process_secondary_preload_request
* Executes secondary-cache management work inside the dedicated preload worker.
* Parameters: request = preload/reset request to process.
* Returns: 1 on success, 0 on failure.
***************************************************/
static uint8_t render_process_secondary_preload_request(const render_request_t *request)
{
    if (!request)
    {
        return 0;
    }

    if (request->type == RENDER_REQUEST_PRELOAD_SECONDARY)
    {
        uint8_t i = 0;
        uint8_t ok = 1;

        for (i = 0; i < request->payload.preload_secondary.count; i++)
        {
            ok = (uint8_t)(ok && render_image_preload_secondary(&g_render_state,
                                                                request->payload.preload_secondary.names[i],
                                                                request->payload.preload_secondary.width,
                                                                request->payload.preload_secondary.height));
        }
#if defined(ESP_PLATFORM)
        render_core_set_secondary_preload_ready(ok);
#endif
        printf("PB:%u\n", ok);
        return ok;
    }

    if (request->type == RENDER_REQUEST_RESET_SECONDARY_CACHE)
    {
        render_cache_reset_secondary(&g_render_state);
#if defined(ESP_PLATFORM)
        render_core_set_secondary_preload_ready(0);
#endif
        return 1;
    }

    return 0;
}

/************************************************
* render_core_run_loop
* Runs the render queue pump and pushes queued requests to the display.
* Parameters: none.
* Returns: void.
***************************************************/
static void render_core_run_loop(void)
{
    for (;;)
    {
        render_request_t request = {0};

#if defined(ESP_PLATFORM)
        /* Sleep here until at least one render request arrives. */
        if (xQueueReceive(g_render_state.request_q, &request, portMAX_DELAY) != pdTRUE)
        {
            continue;
        }
        printf("R:%s\n", render_request_debug_name(&request));
#else
        break;
#endif

        /* Process requests in FIFO order so cache-control work cannot be dropped by later frames. */
        (void)render_process_request(&request);

#if defined(ESP_PLATFORM)
        /* Only visible frame draws are paced; cache-management requests should run immediately. */
        if (render_request_needs_pacing(&request))
        {
            vTaskDelay(pdMS_TO_TICKS(RENDER_INTER_FRAME_DELAY_MS));
        }
#endif
    }
}

/************************************************
* render_core_secondary_preload_run_loop
* Runs the dedicated worker that serializes secondary-cache load and reset requests.
* Parameters: none.
* Returns: void.
***************************************************/
static void render_core_secondary_preload_run_loop(void)
{
    for (;;)
    {
        render_request_t request = {0};

#if defined(ESP_PLATFORM)
        if (xQueueReceive(g_render_state.secondary_preload_q, &request, portMAX_DELAY) != pdTRUE)
        {
            continue;
        }
        printf("R:%s\n", render_request_debug_name(&request));
#else
        break;
#endif

        (void)render_process_secondary_preload_request(&request);
    }
}

#if defined(ESP_PLATFORM)
/************************************************
* render_core_secondary_preload_task_entry
* FreeRTOS entry point for the secondary preload worker task.
* Parameters: arg = unused task context.
* Returns: never.
***************************************************/
static void render_core_secondary_preload_task_entry(void *arg)
{
    (void)arg;

    g_render_state.secondary_preload_task_handle = xTaskGetCurrentTaskHandle();
    render_core_secondary_preload_run_loop();
    vTaskDelete(0);
}
#endif

/************************************************
* render_core_init
* Initializes render state, display access, and the render request queue.
* Parameters: none.
* Returns: void.
***************************************************/
void render_core_init(void)
{
    if (g_render_initialized)
    {
        return;
    }

    /* Reset all render-owned state so startup always begins from a known baseline. */
    memset(&g_render_state, 0, sizeof(g_render_state));
    /* Start both cache banks empty before any app requests begin. */
    render_cache_init(&g_render_state);
    /* Text init performs the shared display bring-up for the render subsystem. */
    render_text_init_display();

#if defined(ESP_PLATFORM)
    /* The request queue is the handoff point between callers and the render task. */
    g_render_state.sync_events = xEventGroupCreate();
    g_render_state.cache_lock = xSemaphoreCreateMutex();
    g_render_state.request_q = xQueueCreate(RENDER_QUEUE_LENGTH, sizeof(render_request_t));
    g_render_state.secondary_preload_q =
        xQueueCreate(RENDER_SECONDARY_PRELOAD_QUEUE_LENGTH, sizeof(render_request_t));
    printf("PS:CFG:%u\n",
#if defined(CONFIG_SPIRAM)
           1u
#else
           0u
#endif
    );
    printf("PS:HT:%lu\n", (unsigned long)heap_caps_get_total_size(MALLOC_CAP_SPIRAM));
    printf("PS:HF:%lu\n", (unsigned long)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    printf("PS:HL:%lu\n", (unsigned long)heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM));
#endif
    g_render_initialized = 1;
}

/************************************************
* render_core_app_task
* Runs the render service loop that consumes queued render requests.
* Parameters: ctx = unused task context.
* Returns: 1 on graceful completion, 0 on failure.
***************************************************/
uint8_t render_core_app_task(void *ctx)
{
    (void)ctx;

    render_core_init();
#if defined(ESP_PLATFORM)
    g_render_state.task_handle = xTaskGetCurrentTaskHandle();
    if (!g_render_state.secondary_preload_task_handle)
    {
        (void)xTaskCreate(render_core_secondary_preload_task_entry,
                          "render_preload",
                          RENDER_SECONDARY_PRELOAD_TASK_STACK_WORDS,
                          0,
                          RENDER_SECONDARY_PRELOAD_TASK_PRIORITY,
                          &g_render_state.secondary_preload_task_handle);
    }
#endif
    printf("DBG:R4\n");
    render_core_run_loop();
    return 1;
}

/************************************************
* render_core_bind_reader
* Registers the card reader used to load queued frame requests.
* Parameters: dev = active card reader instance.
* Returns: void.
***************************************************/
void render_core_bind_reader(card_reader_state_t *dev)
{
    /* Store the active SD reader so later image requests know where to load from. */
    g_render_state.reader = dev;
}

/************************************************
* render_core_reset_caches
* Clears both render cache banks managed by the render subsystem.
* Parameters: none.
* Returns: void.
***************************************************/
void render_core_reset_caches(void)
{
    /* Keep cache ownership centralized in render so app_mgr never manipulates cache state directly. */
    render_cache_reset_all(&g_render_state);
}

/************************************************
* render_core_reset_secondary_cache
* Clears only the secondary render cache bank.
* Parameters: none.
* Returns: void.
***************************************************/
void render_core_reset_secondary_cache(void)
{
    /* The secondary cache is render-owned even when app code decides when to rotate it. */
    render_cache_reset_secondary(&g_render_state);
#if defined(ESP_PLATFORM)
    render_core_set_secondary_preload_ready(0);
#endif
}

/************************************************
* render_core_drop_pending_draws
* Removes queued image and text draw requests while preserving render control requests.
* Parameters: none.
* Returns: void.
***************************************************/
void render_core_drop_pending_draws(void)
{
#if defined(ESP_PLATFORM)
    render_request_t pending[RENDER_QUEUE_LENGTH];
    uint8_t keep_count = 0;
    uint8_t i = 0;

    if (!g_render_state.request_q)
    {
        return;
    }

    /* Drain the queue so we can rebuild it without stale draw work. */
    while (xQueueReceive(g_render_state.request_q, &pending[keep_count], 0) == pdTRUE)
    {
        if (pending[keep_count].type == RENDER_REQUEST_PRELOAD_SECONDARY ||
            pending[keep_count].type == RENDER_REQUEST_RESET_SECONDARY_CACHE)
        {
            keep_count++;
        }
    }

    /* Requeue only render-owned control requests in their original order. */
    (void)xQueueReset(g_render_state.request_q);
    for (i = 0; i < keep_count; i++)
    {
        (void)xQueueSendToBack(g_render_state.request_q, &pending[i], 0);
    }
#endif
}

/************************************************
* render_core_secondary_preload_ready
* Reports whether the current secondary preload batch finished successfully.
* Parameters: none.
* Returns: 1 when ready, 0 otherwise.
***************************************************/
uint8_t render_core_secondary_preload_ready(void)
{
#if defined(ESP_PLATFORM)
    if (!g_render_state.sync_events)
    {
        printf("PR:0\n");
        return 0;
    }

    {
        uint8_t ready =
            (xEventGroupGetBits(g_render_state.sync_events) & RENDER_EVENT_SECONDARY_PRELOAD_READY) != 0;
        printf("PR:%u\n", ready);
        return ready;
    }
#else
    return 0;
#endif
}

/************************************************
* render_core_secondary_has_raw565
* Reports whether one RAW565 image is staged in the secondary cache.
* Parameters: name = file name, width = image width, height = image height.
* Returns: 1 when ready, 0 otherwise.
***************************************************/
uint8_t render_core_secondary_has_raw565(const char *name, uint16_t width, uint16_t height)
{
    uint32_t expected_size = (uint32_t)width * (uint32_t)height * RENDER_BYTES_PER_PIXEL;

    if (!name)
    {
        return 0;
    }

    return render_cache_has_secondary(&g_render_state, name, expected_size);
}

/************************************************
* render_core_queue_raw565
* Queues a RAW565 frame request for the render pipeline.
* Parameters: name = file name, width = frame width, height = frame height.
* Returns: 1 on success, 0 on failure.
***************************************************/
uint8_t render_core_queue_raw565(const char *name, uint16_t width, uint16_t height)
{
    render_request_t request = {0};

    /* Let the image helper validate inputs and fill the request payload. */
    if (!render_image_queue_request(&request, name, width, height))
    {
        return 0;
    }

#if defined(ESP_PLATFORM)
    if (g_render_state.request_q)
    {
        /* Append the frame request so the render task can work through up to four queued images. */
        UBaseType_t queued = uxQueueMessagesWaiting(g_render_state.request_q);
        uint8_t ok = xQueueSendToBack(g_render_state.request_q, &request, 0) == pdTRUE;
        printf("%s:%u:%s\n", ok ? "Q" : "Q!", (unsigned)queued, request.payload.raw565.name);
        return ok;
    }
#endif

    /* Direct rendering is disallowed; only the render service task may consume requests. */
    printf("Q?:%s\n", request.payload.raw565.name);
    return 0;
}

/************************************************
* render_core_queue_preload_raw565_secondary
* Queues a RAW565 preload request for the secondary cache.
* Parameters: name = file name, width = image width, height = image height.
* Returns: 1 on success, 0 on failure.
***************************************************/
uint8_t render_core_queue_preload_raw565_secondary(const char *name, uint16_t width, uint16_t height)
{
    render_request_t request = {0};

    if (!render_image_queue_preload_secondary_request(&request, name, width, height))
    {
        return 0;
    }

#if defined(ESP_PLATFORM)
    if (g_render_state.secondary_preload_q)
    {
        uint8_t ok = xQueueSendToBack(g_render_state.secondary_preload_q, &request, 0) == pdTRUE;
        printf("%s:P:%s\n", ok ? "Q" : "Q!", request.payload.raw565.name);
        return ok;
    }
#endif

    printf("Q?:P:%s\n", request.payload.raw565.name);
    return 0;
}

/************************************************
* render_core_queue_preload_raw565_secondary_list
* Queues a batch of RAW565 preload requests for the secondary cache.
* Parameters: names = file name list, count = number of files, width/height = image size.
* Returns: 1 on success, 0 on failure.
***************************************************/
uint8_t render_core_queue_preload_raw565_secondary_list(const char *const *names,
                                                        uint8_t count,
                                                        uint16_t width,
                                                        uint16_t height)
{
    render_request_t request = {0};

    if (!render_image_queue_preload_secondary_list_request(&request, names, count, width, height))
    {
        return 0;
    }

#if defined(ESP_PLATFORM)
    if (g_render_state.secondary_preload_q)
    {
        render_core_set_secondary_preload_ready(0);
        /* Secondary preload work is serialized in its own worker task. */
        uint8_t ok = xQueueSendToBack(g_render_state.secondary_preload_q, &request, 0) == pdTRUE;
        printf("%s:P:%u\n", ok ? "Q" : "Q!", (unsigned)count);
        return ok;
    }
#endif

    printf("Q?:P:%u\n", (unsigned)count);
    return 0;
}

/************************************************
* render_core_queue_reset_secondary_cache
* Queues a request to clear the secondary cache inside the render task.
* Parameters: none.
* Returns: 1 on success, 0 on failure.
***************************************************/
uint8_t render_core_queue_reset_secondary_cache(void)
{
    render_request_t request = {0};

    request.type = RENDER_REQUEST_RESET_SECONDARY_CACHE;

#if defined(ESP_PLATFORM)
    if (g_render_state.secondary_preload_q)
    {
        /* Reset requests are handled in FIFO order by the preload worker before later staged loads. */
        uint8_t ok = xQueueSendToBack(g_render_state.secondary_preload_q, &request, 0) == pdTRUE;
        printf("%s:RC\n", ok ? "Q" : "Q!");
        return ok;
    }
#endif

    printf("Q?:RC\n");
    return 0;
}

/************************************************
* render_core_preload_raw565_primary
* Loads a RAW565 image into the primary cache without queueing it for display.
* Parameters: name = file name, width = image width, height = image height.
* Returns: 1 on success, 0 on failure.
***************************************************/
uint8_t render_core_preload_raw565_primary(const char *name, uint16_t width, uint16_t height)
{
    /* Only the render subsystem may touch image loading and cache ownership directly. */
    return render_image_preload_primary(&g_render_state, name, width, height);
}

/************************************************
* render_core_preload_raw565_secondary
* Loads a RAW565 image into the secondary cache without queueing it for display.
* Parameters: name = file name, width = image width, height = image height.
* Returns: 1 on success, 0 on failure.
***************************************************/
uint8_t render_core_preload_raw565_secondary(const char *name, uint16_t width, uint16_t height)
{
    /* Only the render subsystem may touch image loading and cache ownership directly. */
    return render_image_preload_secondary(&g_render_state, name, width, height);
}

/************************************************
* render_core_show_text_screen
* Queues a simple text-screen request for the render pipeline.
* Parameters: line0-line3 = optional lines of text.
* Returns: 1 on success, 0 on failure.
***************************************************/
uint8_t render_core_show_text_screen(const char *line0,
                                     const char *line1,
                                     const char *line2,
                                     const char *line3)
{
    render_request_t request = {0};

    /* Let the text helper normalize NULL lines and build the request payload. */
    if (!render_text_queue_request(&request, line0, line1, line2, line3))
    {
        return 0;
    }

#if defined(ESP_PLATFORM)
    if (g_render_state.request_q)
    {
        /* Queue the text update behind older work until the four-slot queue fills. */
        uint8_t ok = xQueueSendToBack(g_render_state.request_q, &request, 0) == pdTRUE;
        printf("%s:TXT\n", ok ? "Q" : "Q!");
        return ok;
    }
#endif

    /* Direct rendering is disallowed; only the render service task may consume requests. */
    printf("Q?:TXT\n");
    return 0;
}
