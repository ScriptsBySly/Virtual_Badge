#include "system/render/render_api.h"
#include "system/render/render_core.h"

/************************************************
* render_init
* Initializes the render subsystem through the internal core layer.
* Parameters: none.
* Returns: void.
***************************************************/
void render_init(void)
{
    /* Keep the public API thin by delegating initialization to the core layer. */
    render_core_init();
}

/************************************************
* render_app_task
* Runs the render service task owned by the app manager.
* Parameters: ctx = unused task context.
* Returns: 1 on graceful completion, 0 on failure.
***************************************************/
uint8_t render_app_task(void *ctx)
{
    return render_core_app_task(ctx);
}

/************************************************
* render_bind_reader
* Associates the active card reader with the render subsystem.
* Parameters: dev = active card reader instance.
* Returns: void.
***************************************************/
void render_bind_reader(card_reader_state_t *dev)
{
    /* The public API only forwards the active reader to the internal coordinator. */
    render_core_bind_reader(dev);
}

/************************************************
* render_reset_caches
* Clears all render-owned image caches before a display app starts using them.
* Parameters: none.
* Returns: void.
***************************************************/
void render_reset_caches(void)
{
    /* Cache lifetime policy is owned by the render subsystem, even when app_mgr triggers it. */
    render_core_reset_caches();
}

/************************************************
* render_queue_raw565
* Queues a RAW565 image request for asynchronous or immediate rendering.
* Parameters: name = file name, width = image width, height = image height.
* Returns: 1 on success, 0 on failure.
***************************************************/
uint8_t render_queue_raw565(const char *name, uint16_t width, uint16_t height)
{
    /* Image requests always flow through the core so task/queue behavior stays centralized. */
    return render_core_queue_raw565(name, width, height);
}

/************************************************
* render_preload_raw565_primary
* Loads a RAW565 image into the primary cache before it is needed on screen.
* Parameters: name = file name, width = image width, height = image height.
* Returns: 1 on success, 0 on failure.
***************************************************/
uint8_t render_preload_raw565_primary(const char *name, uint16_t width, uint16_t height)
{
    /* Preload requests use the same core-owned image path so cache state stays centralized. */
    return render_core_preload_raw565_primary(name, width, height);
}

/************************************************
* render_show_text_screen
* Queues a simple four-line text screen for rendering.
* Parameters: line0-line3 = optional text lines to display.
* Returns: 1 on success, 0 on failure.
***************************************************/
uint8_t render_show_text_screen(const char *line0,
                                const char *line1,
                                const char *line2,
                                const char *line3)
{
    /* Text requests use the same core entry point as images for consistent dispatch. */
    return render_core_show_text_screen(line0, line1, line2, line3);
}
