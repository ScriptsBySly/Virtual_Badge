#include "drivers/display/display_spi.h"

#include "hal/hal.h"

// Common ST7735 1.8" 128x160 defaults.
#ifndef TFT_X_OFFSET
#define TFT_X_OFFSET 0u
#endif

#ifndef TFT_Y_OFFSET
#define TFT_Y_OFFSET 0u
#endif

enum {
    DISPLAY_CMD_SWRESET = 0x01,
    DISPLAY_CMD_SLPOUT = 0x11,
    DISPLAY_CMD_DISPON = 0x29,
    DISPLAY_CMD_CASET = 0x2A,
    DISPLAY_CMD_RASET = 0x2B,
    DISPLAY_CMD_RAMWR = 0x2C,
    DISPLAY_CMD_MADCTL = 0x36,
    DISPLAY_CMD_COLMOD = 0x3A,
    DISPLAY_COLOR_MODE_16BIT = 0x05,
    DISPLAY_MADCTL_DEFAULT = 0x00,
    DISPLAY_RESET_LOW_MS = 20,
    DISPLAY_RESET_READY_MS = 150,
    DISPLAY_POST_INIT_MS = 50,
};

/************************************************
* display_spi_write_cmd
* Sends a single display command byte.
* Parameters: cmd = controller command byte.
* Returns: void.
***************************************************/
static void display_spi_write_cmd(uint8_t cmd)
{
    /* Drive D/C low so the controller interprets the byte as a command. */
    hal_tft_dc_low();
    hal_tft_cs_low();
    hal_spi_tft_write(cmd);
    hal_tft_cs_high();
}

/************************************************
* display_spi_write_data
* Sends a single display data byte.
* Parameters: data = controller data byte.
* Returns: void.
***************************************************/
static void display_spi_write_data(uint8_t data)
{
    /* Drive D/C high so the controller interprets the byte as data. */
    hal_tft_dc_high();
    hal_tft_cs_low();
    hal_spi_tft_write(data);
    hal_tft_cs_high();
}

/************************************************
* display_spi_write_u16
* Sends a 16-bit value as big-endian display data.
* Parameters: value = 16-bit controller value.
* Returns: void.
***************************************************/
static void display_spi_write_u16(uint16_t value)
{
    display_spi_write_data((uint8_t)(value >> 8));
    display_spi_write_data((uint8_t)(value & 0xFF));
}

/************************************************
* display_spi_reset
* Toggles the hardware reset line for the panel.
* Parameters: none.
* Returns: void.
***************************************************/
static void display_spi_reset(void)
{
    hal_tft_rst_low();
    hal_delay_ms(DISPLAY_RESET_LOW_MS);
    hal_tft_rst_high();
    hal_delay_ms(DISPLAY_RESET_READY_MS);
}

/************************************************
* display_spi_init
* Initializes the ST7735 transport and basic panel state.
* Parameters: none.
* Returns: void.
***************************************************/
void display_spi_init(void)
{
    /* Start at the configured fast bus speed used for display updates. */
    hal_spi_tft_set_speed_fast();
    display_spi_reset();

    /* Reset the controller state machine before configuration writes. */
    display_spi_write_cmd(DISPLAY_CMD_SWRESET);
    hal_delay_ms(DISPLAY_RESET_READY_MS);

    /* Exit sleep mode so the panel accepts subsequent configuration. */
    display_spi_write_cmd(DISPLAY_CMD_SLPOUT);
    hal_delay_ms(DISPLAY_RESET_READY_MS);

    /* Configure the panel for 16-bit RGB565 pixel transfers. */
    display_spi_write_cmd(DISPLAY_CMD_COLMOD);
    display_spi_write_data(DISPLAY_COLOR_MODE_16BIT);

    /* Use the default memory access orientation for this board. */
    display_spi_write_cmd(DISPLAY_CMD_MADCTL);
    display_spi_write_data(DISPLAY_MADCTL_DEFAULT);

    /* Turn the display on after the basic controller setup is complete. */
    display_spi_write_cmd(DISPLAY_CMD_DISPON);
    hal_delay_ms(DISPLAY_POST_INIT_MS);
}

/************************************************
* display_spi_set_addr_window
* Programs the controller address window for pixel writes.
* Parameters: x0/y0/x1/y1 = inclusive pixel bounds.
* Returns: void.
***************************************************/
void display_spi_set_addr_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
    /* Apply panel-specific offsets before programming the controller window. */
    x0 = (uint16_t)(x0 + TFT_X_OFFSET);
    x1 = (uint16_t)(x1 + TFT_X_OFFSET);
    y0 = (uint16_t)(y0 + TFT_Y_OFFSET);
    y1 = (uint16_t)(y1 + TFT_Y_OFFSET);

    /* CASET/RASET define the inclusive bounds for the next RAMWR stream. */
    display_spi_write_cmd(DISPLAY_CMD_CASET);
    display_spi_write_u16(x0);
    display_spi_write_u16(x1);

    display_spi_write_cmd(DISPLAY_CMD_RASET);
    display_spi_write_u16(y0);
    display_spi_write_u16(y1);

    display_spi_write_cmd(DISPLAY_CMD_RAMWR);
}

/************************************************
* display_spi_write_buffer
* Streams a buffer of pixel data to the panel.
* Parameters: data = source bytes, len = byte count.
* Returns: void.
***************************************************/
void display_spi_write_buffer(const uint8_t *data, uint16_t len)
{
    if (!data || len == 0)
    {
        return;
    }

    /* Keep D/C high for the full transfer so the panel stays in data mode. */
    hal_tft_dc_high();
    hal_tft_cs_low();
    hal_spi_tft_write_buffer(data, len);
    hal_tft_cs_high();
}
