#include "system/render/render_text.h"

#include "drivers/display/display_api.h"

enum {
    RENDER_TEXT_X_ORIGIN = 0u,
    RENDER_TEXT_LINE0 = 0u,
    RENDER_TEXT_LINE1,
    RENDER_TEXT_LINE2,
    RENDER_TEXT_LINE3,
};

static const char k_render_text_image_load_error[] = "SD IMG FAIL";

/************************************************
* render_copy_string
* Copies a nullable string into a fixed-size text buffer.
* Parameters: dst = destination buffer, capacity = buffer size, src = source string.
* Returns: void.
***************************************************/
static void render_copy_string(char *dst, uint32_t capacity, const char *src)
{
    uint32_t i = 0;

    /* Refuse to write when the caller gives us no usable destination buffer. */
    if (!dst || capacity == 0u)
    {
        return;
    }

    /* Treat a NULL source as an empty string so callers can omit optional lines. */
    if (!src)
    {
        dst[0] = '\0';
        return;
    }

    /* Copy until the source ends or until we need to reserve space for the terminator. */
    for (i = 0; i < (capacity - 1u) && src[i]; i++)
    {
        dst[i] = src[i];
    }
    /* Always terminate the destination buffer, even when truncation happened. */
    dst[i] = '\0';
}

/************************************************
* render_draw_text_screen_lines
* Draws a prepared four-line text screen to the display.
* Parameters: lines = text lines to render.
* Returns: void.
***************************************************/
static void render_draw_text_screen_lines(const char lines[RENDER_TEXT_LINE_COUNT][RENDER_TEXT_LINE_CAPACITY])
{
    uint8_t i = 0;

    /* Clear the screen first so a shorter message does not leave stale text behind. */
    display_fill_color(RENDER_TEXT_BG_COLOR);
    for (i = 0; i < RENDER_TEXT_LINE_COUNT; i++)
    {
        /* Skip empty lines so callers can provide sparse four-line layouts. */
        if (lines[i][0] != '\0')
        {
            /* Each line is drawn at a fixed vertical step to form a simple text stack. */
            display_draw_text(RENDER_TEXT_X_ORIGIN,
                              (uint16_t)(i * RENDER_TEXT_LINE_SPACING),
                              lines[i],
                              RENDER_TEXT_FG_COLOR,
                              RENDER_TEXT_BG_COLOR);
        }
    }
}

/************************************************
* render_text_init_display
* Initializes the display for text rendering and clears the background.
* Parameters: none.
* Returns: void.
***************************************************/
void render_text_init_display(void)
{
    /* Text rendering owns the initial display bring-up for the render subsystem. */
    display_init();
    /* Start from a known blank background before any requests are processed. */
    display_fill_color(RENDER_TEXT_BG_COLOR);
}

/************************************************
* render_text_queue_request
* Populates a render request with text-screen content.
* Parameters: request = output request, line0-line3 = optional text lines.
* Returns: 1 on success, 0 on failure.
***************************************************/
uint8_t render_text_queue_request(render_request_t *request,
                                  const char *line0,
                                  const char *line1,
                                  const char *line2,
                                  const char *line3)
{
    /* The caller must provide a request object for us to populate. */
    if (!request)
    {
        return 0;
    }

    /* Mark this request so render_core routes it through the text pipeline. */
    request->type = RENDER_REQUEST_TEXT_SCREEN;
    /* Copy each optional line into the fixed-size request payload buffers. */
    render_copy_string(request->payload.text_screen.lines[RENDER_TEXT_LINE0],
                       sizeof(request->payload.text_screen.lines[RENDER_TEXT_LINE0]),
                       line0);
    render_copy_string(request->payload.text_screen.lines[RENDER_TEXT_LINE1],
                       sizeof(request->payload.text_screen.lines[RENDER_TEXT_LINE1]),
                       line1);
    render_copy_string(request->payload.text_screen.lines[RENDER_TEXT_LINE2],
                       sizeof(request->payload.text_screen.lines[RENDER_TEXT_LINE2]),
                       line2);
    render_copy_string(request->payload.text_screen.lines[RENDER_TEXT_LINE3],
                       sizeof(request->payload.text_screen.lines[RENDER_TEXT_LINE3]),
                       line3);
    return 1;
}

/************************************************
* render_text_process_request
* Renders a prepared text-screen request to the display.
* Parameters: request = text request to process.
* Returns: 1 on success, 0 on failure.
***************************************************/
uint8_t render_text_process_request(const render_request_t *request)
{
    /* Reject missing requests instead of dereferencing a NULL pointer. */
    if (!request)
    {
        return 0;
    }

    /* A prepared text request already contains everything needed to draw the screen. */
    render_draw_text_screen_lines(request->payload.text_screen.lines);
    return 1;
}

/************************************************
* render_text_show_image_load_error
* Displays a standard text error screen for failed image loads.
* Parameters: name = image file name that failed to load.
* Returns: 1 on success, 0 on failure.
***************************************************/
uint8_t render_text_show_image_load_error(const char *name)
{
    render_request_t request = {0};

    /* Reuse the normal text request path so error screens follow the same rendering flow. */
    if (!render_text_queue_request(&request, k_render_text_image_load_error, name, NULL, NULL))
    {
        return 0;
    }

    /* Draw the synthesized error request immediately. */
    return render_text_process_request(&request);
}
