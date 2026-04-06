#include "system/render/render_core.h"

#include "system/render/render_common.h"
#include "system/render/render_image.h"
#include "system/render/render_text.h"

#include <string.h>

static render_state_t g_render_state;

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

    /* Reject unknown request kinds so the caller gets a clear failure signal. */
    return 0;
}

#if defined(ESP_PLATFORM)
/************************************************
* render_task
* Runs the render queue pump and pushes queued requests to the display.
* Parameters: arg = unused.
* Returns: never returns.
***************************************************/
static void render_task(void *arg)
{
    (void)arg;

    for (;;)
    {
        render_request_t request = {0};
        uint8_t has_request = 0;

        /* Drain the queue and keep only the newest request so rendering does not fall behind. */
        while (xQueueReceive(g_render_state.request_q, &request, 0) == pdTRUE)
        {
            has_request = 1;
        }

        /* Only hand work to the pipeline when at least one request was waiting. */
        if (has_request)
        {
            (void)render_process_request(&request);
        }

        /* Sleep for the configured render cadence before polling again. */
        vTaskDelay(pdMS_TO_TICKS(RENDER_TASK_PERIOD_MS));
    }
}
#endif

/************************************************
* render_core_init
* Initializes render state, display access, and the render task.
* Parameters: none.
* Returns: void.
***************************************************/
void render_core_init(void)
{
    /* Reset all render-owned state so startup always begins from a known baseline. */
    memset(&g_render_state, 0, sizeof(g_render_state));
    /* Text init performs the shared display bring-up for the render subsystem. */
    render_text_init_display();

#if defined(ESP_PLATFORM)
    /* The request queue is the handoff point between callers and the render task. */
    g_render_state.request_q = xQueueCreate(RENDER_QUEUE_LENGTH, sizeof(render_request_t));
    if (g_render_state.request_q)
    {
        /* Start the background render worker only when the queue was created successfully. */
        (void)xTaskCreate(render_task,
                          "render",
                          RENDER_TASK_STACK_WORDS,
                          0,
                          RENDER_TASK_PRIORITY,
                          &g_render_state.task_handle);
    }
#endif
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
        /* Overwrite any stale pending work so the newest frame request wins. */
        return xQueueOverwrite(g_render_state.request_q, &request) == pdTRUE;
    }
#endif

    /* On non-RTOS builds, process the request immediately in the caller context. */
    return render_process_request(&request);
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
        /* Text requests also replace older pending work so the screen reflects the latest state. */
        return xQueueOverwrite(g_render_state.request_q, &request) == pdTRUE;
    }
#endif

    /* Without a background task, draw the text request immediately. */
    return render_process_request(&request);
}
