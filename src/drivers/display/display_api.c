#include "drivers/display/display_api.h"
#include "drivers/display/display_core.h"

/************************************************
* display_init
* Initializes the display subsystem.
* Parameters: none.
* Returns: void.
***************************************************/
void display_init(void)
{
    display_core_init();
}

/************************************************
* display_fill_color
* Fills the entire display with a solid RGB565 color.
* Parameters: color = RGB565 color value.
* Returns: void.
***************************************************/
void display_fill_color(uint16_t color)
{
    display_core_fill_color(color);
}

/************************************************
* display_set_addr_window
* Sets the active write window from the origin to width/height.
* Parameters: width = window width in pixels, height = window height in pixels.
* Returns: void.
***************************************************/
void display_set_addr_window(uint16_t width, uint16_t height)
{
    display_core_set_addr_window(width, height);
}

/************************************************
* display_stream_bytes
* Streams raw pixel bytes to the active display window.
* Parameters: data = source bytes, len = byte count.
* Returns: void.
***************************************************/
void display_stream_bytes(const uint8_t *data, uint16_t len)
{
    display_core_stream_bytes(data, len);
}

/************************************************
* display_draw_char
* Draws a single 5x7 glyph into a 6x8 cell.
* Parameters: x/y = top-left pixel, c = character, fg/bg = RGB565 colors.
* Returns: void.
***************************************************/
void display_draw_char(uint16_t x, uint16_t y, char c, uint16_t fg, uint16_t bg)
{
    display_core_draw_char(x, y, c, fg, bg);
}

/************************************************
* display_draw_text
* Draws a null-terminated string using the built-in bitmap font.
* Parameters: x/y = top-left pixel, text = string, fg/bg = RGB565 colors.
* Returns: void.
***************************************************/
void display_draw_text(uint16_t x, uint16_t y, const char *text, uint16_t fg, uint16_t bg)
{
    display_core_draw_text(x, y, text, fg, bg);
}
