#include "drivers/card_reader/card_reader_api.h"
#include "drivers/card_reader/card_reader_core.h"

card_reader_state_t *card_reader_file_open(void)
{
    return card_reader_core_open();
}

uint8_t card_reader_file_read(card_reader_state_t *dev,
const char *name,
uint32_t expected_size,
void (*sink)(const uint8_t *data, uint16_t len, void *ctx),
void *ctx)
{
    return card_reader_core_read(dev, name, expected_size, sink, ctx);
}

uint8_t card_reader_file_write(card_reader_state_t *dev,
const char *name,
const uint8_t *data,
uint32_t len)
{
    return card_reader_core_write(dev, name, data, len);
}

void card_reader_file_close(card_reader_state_t *dev)
{
    card_reader_core_close(dev);
}
