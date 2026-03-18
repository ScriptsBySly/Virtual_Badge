#include "drivers/card_reader/card_reader_spi.h"

#include "hal/hal.h"

enum {
    CARD_READER_SPI_R1_POLL_MAX = 100,
    CARD_READER_SPI_R7_BYTES = 4,
};

#define CARD_READER_SPI_DUMMY_BYTE 0xFF
#define CARD_READER_SPI_READY 0xFF
#define CARD_READER_SPI_R1_BUSY_MASK 0x80
#define CARD_READER_SPI_R1_READY_VALUE 0x00

/************************************************
* card_reader_spi_deselect
* Deasserts SD/TFT chip selects and clocks a dummy byte.
* Parameters: none.
* Returns: void.
***************************************************/
void card_reader_spi_deselect(void)
{
    /* Release the SPI bus, then clock a byte to flush any pending response. */
#if !HAL_HAS_SEPARATE_SPI_BUSES
    /* Keep the display inactive when sharing a bus with the SD card. */
    hal_tft_cs_high();
#endif
    hal_sd_cs_high();
    hal_spi_sd_transfer(CARD_READER_SPI_DUMMY_BYTE);
}

/************************************************
* card_reader_spi_idle_clocks
* Sends idle clocks with chip select high (SD init).
* Parameters: count = number of dummy clock bytes.
* Returns: void.
***************************************************/
void card_reader_spi_idle_clocks(uint8_t count)
{
    /* Keep the display inactive when sharing a bus with the SD card. */
#if !HAL_HAS_SEPARATE_SPI_BUSES
    hal_tft_cs_high();
#endif
    hal_sd_cs_high();
    for (uint8_t i = 0; i < count; i++)
    {
        /* Provide idle clocks with chip select high during SD init. */
        hal_spi_sd_transfer(CARD_READER_SPI_DUMMY_BYTE);
    }
}

/************************************************
* card_reader_spi_select
* Asserts SD chip select and clocks a dummy byte.
* Parameters: none.
* Returns: 1 on success.
***************************************************/
uint8_t card_reader_spi_select(void)
{
    /* Keep the display inactive when sharing a bus with the SD card. */
#if !HAL_HAS_SEPARATE_SPI_BUSES
    hal_tft_cs_high();
#endif
    hal_sd_cs_low();
    /* Provide an initial clock edge after asserting chip select. */
    hal_spi_sd_transfer(CARD_READER_SPI_DUMMY_BYTE);
    return 1;
}

/************************************************
* card_reader_spi_wait_ready
* Waits for SD to return 0xFF (ready) with timeout.
* Parameters: timeout_ms = max wait time in milliseconds.
* Returns: 1 if ready, 0 on timeout.
***************************************************/
uint8_t card_reader_spi_wait_ready(uint16_t timeout_ms)
{
    while (timeout_ms--)
    {
        /* Poll until the card signals it is ready for the next transfer. */
        if (hal_spi_sd_transfer(CARD_READER_SPI_DUMMY_BYTE) == CARD_READER_SPI_READY)
        {
            return 1;
        }
        hal_delay_ms(1);
    }
    return 0;
}

/************************************************
* card_reader_spi_send_cmd
* Sends an SD command and returns R1.
* Parameters: cmd (SD command index), arg (32-bit argument),
*             crc (command CRC), response_r7 (optional 4-byte response),
*             skip_select (nonzero to skip deselect/select).
* Returns: R1 response byte (0xFF on timeout/invalid).
***************************************************/
uint8_t card_reader_spi_send_cmd(uint8_t cmd,
uint32_t arg,
uint8_t crc,
uint8_t *response_r7,
uint8_t skip_select)
{
    if (!skip_select)
    {
        card_reader_spi_deselect();
        card_reader_spi_select();
    }
    if (!card_reader_spi_wait_ready(250))
    {
        return 0xFF;
    }

    /* Send the command frame followed by its argument and CRC. */
    /*Command*/
    hal_spi_sd_transfer(0x40 | cmd);
    /*Argument*/
    hal_spi_sd_transfer((uint8_t)(arg >> 24));
    hal_spi_sd_transfer((uint8_t)(arg >> 16));
    hal_spi_sd_transfer((uint8_t)(arg >> 8));
    hal_spi_sd_transfer((uint8_t)(arg));
    /*CRC*/
    hal_spi_sd_transfer(crc);

    /* Read the SD R1 status byte, which may be delayed after a command. */
    uint8_t r1 = CARD_READER_SPI_READY;
    for (uint8_t i = 0; i < CARD_READER_SPI_R1_POLL_MAX; i++)
    {
        /* Keep clocking until the card responds with a status byte (MSB cleared). */
        r1 = hal_spi_sd_transfer(CARD_READER_SPI_DUMMY_BYTE);
        if ((r1 & CARD_READER_SPI_R1_BUSY_MASK) == CARD_READER_SPI_R1_READY_VALUE)
        {
            break;
        }
    }

    /* Read the extra 4-byte payload only when the caller requests it (CMD8/CMD58). */
    if (response_r7)
    {
        for (uint8_t i = 0; i < CARD_READER_SPI_R7_BYTES; i++)
        {
            response_r7[i] = hal_spi_sd_transfer(CARD_READER_SPI_DUMMY_BYTE);
        }
    }
    return r1;
}

/************************************************
* card_reader_spi_set_speed
* Switches SD SPI speed (FAST or SLOW).
* Parameters: speed = CARD_READER_SPI_SPEED_FAST or _SLOW.
* Returns: void.
***************************************************/
void card_reader_spi_set_speed(card_reader_spi_speed_t speed)
{
    switch (speed)
    {
        case CARD_READER_SPI_SPEED_FAST:
        /* Use the higher bus speed after initialization. */
        hal_spi_sd_set_speed_fast();
        break;
        case CARD_READER_SPI_SPEED_SLOW:
        default:
        /* Use a conservative speed for initialization and retries. */
        hal_spi_sd_set_speed_very_slow();
        break;
    }
}

/************************************************
* card_reader_spi_transfer_byte
* Transfers a single byte over SD SPI.
* Parameters: data = byte to transmit.
* Returns: received byte.
***************************************************/
uint8_t card_reader_spi_transfer_byte(uint8_t data)
{
    /* Perform a single full-duplex SPI byte transfer. */
    return hal_spi_sd_transfer(data);
}

/************************************************
* card_reader_spi_read_buffer
* Reads a buffer of bytes from SD SPI.
* Parameters: data = output buffer, len = bytes to read.
* Returns: void.
***************************************************/
void card_reader_spi_read_buffer(uint8_t *data, uint16_t len)
{
    /* Read a contiguous block of bytes from the card. */
    hal_spi_sd_read_buffer(data, len);
}
