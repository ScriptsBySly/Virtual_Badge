#include "drivers/card_reader/card_reader_api.h"
#include "drivers/card_reader/card_reader_core.h"

/************************************************
* card_reader_file_open
* Opens a card reader instance through the core layer.
* Parameters: none.
* Returns: initialized card reader instance, or NULL on failure.
***************************************************/
card_reader_state_t *card_reader_file_open(void)
{
    return card_reader_core_open();
}

/************************************************
* card_reader_wait_ready
* Waits until the SD card and FAT layer are ready for file operations.
* Parameters: status_fn = optional wait-status callback, ctx = callback context.
* Returns: ready card reader instance, or NULL on failure.
***************************************************/
card_reader_state_t *card_reader_wait_ready(card_reader_wait_status_fn_t status_fn, void *ctx)
{
    return card_reader_core_wait_ready(status_fn, ctx);
}

/************************************************
* card_reader_file_read
* Reads a file through the core card reader implementation.
* Parameters: dev = card reader instance, name = filename,
*             expected_size = expected byte count or 0 to ignore,
*             sink = chunk callback, ctx = callback context.
* Returns: 1 on success, 0 on failure.
***************************************************/
uint8_t card_reader_file_read(card_reader_state_t *dev,
const char *name,
uint32_t expected_size,
void (*sink)(const uint8_t *data, uint16_t len, void *ctx),
void *ctx)
{
    return card_reader_core_read(dev, name, expected_size, sink, ctx);
}

/************************************************
* card_reader_file_write
* Writes a file through the core card reader implementation.
* Parameters: dev = card reader instance, name = filename,
*             data = input bytes, len = byte count.
* Returns: 1 on success, 0 on failure.
***************************************************/
uint8_t card_reader_file_write(card_reader_state_t *dev,
const char *name,
const uint8_t *data,
uint32_t len)
{
    return card_reader_core_write(dev, name, data, len);
}

/************************************************
* card_reader_file_close
* Closes a card reader instance through the core layer.
* Parameters: dev = card reader instance.
* Returns: void.
***************************************************/
void card_reader_file_close(card_reader_state_t *dev)
{
    card_reader_core_close(dev);
}

/************************************************
* card_reader_describe_status
* Formats SD status lines through the core card reader implementation.
* Parameters: dev = card reader instance, line0-line3 = output buffers.
* Returns: void.
***************************************************/
void card_reader_describe_status(const card_reader_state_t *dev,
char line0[CARD_READER_STATUS_LINE_CAPACITY],
char line1[CARD_READER_STATUS_LINE_CAPACITY],
char line2[CARD_READER_STATUS_LINE_CAPACITY],
char line3[CARD_READER_STATUS_LINE_CAPACITY])
{
    card_reader_core_describe_status(dev, line0, line1, line2, line3);
}
