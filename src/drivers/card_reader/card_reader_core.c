#include "drivers/card_reader/card_reader_core.h"
#include "drivers/card_reader/card_reader_spi.h"
#include "drivers/card_reader/card_reader_fat.h"

#include "hal/hal.h"

#include <stdlib.h>
#include <string.h>

#define CARD_READER_SD_R1_NO_RESPONSE 0xFF
#define CARD_READER_SD_DUMMY_BYTE 0xFF
#define CARD_READER_SD_INIT_DELAY_MS 500
#define CARD_READER_SD_CMD0_RETRY_DELAY_MS 10
#define CARD_READER_SD_ACMD41_DELAY_MS 2
#define CARD_READER_SD_IDLE_CLOCKS_START 20
#define CARD_READER_SD_IDLE_CLOCKS_SHORT 2
#define CARD_READER_SD_IDLE_CLOCKS_RETRY 10
#define CARD_READER_SD_NO_RESP_LIMIT 50

#define CARD_READER_SD_CMD0 0
#define CARD_READER_SD_CMD1 1
#define CARD_READER_SD_CMD8 8
#define CARD_READER_SD_CMD55 55
#define CARD_READER_SD_ACMD41 41
#define CARD_READER_SD_CMD58 58

#define CARD_READER_SD_CMD0_CRC 0x95
#define CARD_READER_SD_CMD8_ARG 0x000001AA
#define CARD_READER_SD_CMD8_CRC 0x87
#define CARD_READER_SD_ACMD41_ARG_HCS 0x40000000
#define CARD_READER_SD_CMD_CRC_DUMMY 0x01

#define CARD_READER_CMD0_ATTEMPTS 10
#define CARD_READER_CMD8_R7_BYTES 4
#define CARD_READER_ACMD41_ATTEMPTS 8000
#define CARD_READER_CMD1_ATTEMPTS 5000

typedef enum {
    SD_R1_STATE_IDLE = 0x01,
    SD_R1_STATE_READY = 0x00,
    SD_R1_STATE_ILLEGAL_CMD = 0x04,
    SD_R1_STATE_CRC_ERROR = 0x08,
    SD_R1_STATE_ERASE_RESET = 0x02,
    SD_R1_STATE_ADDRESS_ERROR = 0x20,
    SD_R1_STATE_PARAMETER_ERROR = 0x40,
} sd_r1_state_t;

typedef enum {
    SD_CMD8_VOLTAGE_27_36 = 0x01,
} sd_cmd8_voltage_t;

typedef enum {
    SD_CMD8_CHECK_PATTERN = 0xAA,
} sd_cmd8_pattern_t;

typedef enum {
    SD_OCR_POWER_UP_STATUS = 0x80,
    SD_OCR_CCS = 0x40,
} sd_ocr_bit_t;

/************************************************
* card_reader_state_reset
* Clears state fields and sets default register values.
* Parameters: state = card reader instance to reset.
* Returns: void.
***************************************************/
static void card_reader_state_reset(card_reader_state_t *state)
{
    /* Avoid touching memory when the caller passes NULL. */
    if (!state)
    {
        return;
    }
    /* Clear state and apply known default register values. */
    memset(state, 0, sizeof(*state));
    state->regs.sd_last_cmd0_r1 = CARD_READER_SD_R1_NO_RESPONSE;
    state->regs.sd_last_cmd8_r1 = CARD_READER_SD_R1_NO_RESPONSE;
    state->regs.sd_last_acmd41_r1 = CARD_READER_SD_R1_NO_RESPONSE;
    state->regs.sd_last_cmd58_r1 = CARD_READER_SD_R1_NO_RESPONSE;
}

/************************************************
* sd_init
* Performs SD card initialization and updates status registers.
* Parameters: state = card reader instance.
* Returns: 1 on success, 0 on failure.
***************************************************/
static uint8_t sd_init(card_reader_state_t *state)
{
    /* Start with chip deselected and slow SPI for initialization. */
    hal_sd_cs_high();
    card_reader_spi_set_speed(CARD_READER_SPI_SPEED_SLOW);
    uint8_t ok = 0;

    /* Allow the card time to power up before issuing commands. */
    hal_delay_ms(CARD_READER_SD_INIT_DELAY_MS);
    /* Provide idle clocks with CS high to enter SPI mode cleanly. */
    card_reader_spi_idle_clocks(CARD_READER_SD_IDLE_CLOCKS_START);

    uint8_t cmd8_response[4] = {0};
    uint8_t r1 = CARD_READER_SD_R1_NO_RESPONSE;
    /* CMD0: reset into SPI mode and wait for IDLE response. */
    for (uint8_t attempt = 0; attempt < CARD_READER_CMD0_ATTEMPTS; attempt++)
    {
        r1 = card_reader_spi_send_cmd(CARD_READER_SD_CMD0, 0, CARD_READER_SD_CMD0_CRC, 0, 0);
        /* CMD0 succeeds when the card reports the IDLE state. */
        if (r1 == SD_R1_STATE_IDLE)
        {
            break;
        }
        /* Brief pause between retries to avoid hammering the card. */
        hal_delay_ms(CARD_READER_SD_CMD0_RETRY_DELAY_MS);
    }
    state->regs.sd_last_cmd0_r1 = r1;
    /* If CMD0 never reached IDLE, stop initialization. */
    if (r1 != SD_R1_STATE_IDLE)
    {
        goto out;
    }

    /* CMD8: check voltage range and echo pattern for SD v2 cards. */
    r1 = card_reader_spi_send_cmd(CARD_READER_SD_CMD8,
                                  CARD_READER_SD_CMD8_ARG,
                                  CARD_READER_SD_CMD8_CRC,
                                  cmd8_response,
                                  0);
    state->regs.sd_last_cmd8_r1 = r1;
    for (uint8_t i = 0; i < CARD_READER_CMD8_R7_BYTES; i++)
    {
        state->regs.sd_last_cmd8_r7[i] = cmd8_response[i];
    }
    /* CMD8 only valid if card stayed IDLE and echoed expected values. */
    if (r1 == SD_R1_STATE_IDLE &&
        cmd8_response[2] == SD_CMD8_VOLTAGE_27_36 &&
        cmd8_response[3] == SD_CMD8_CHECK_PATTERN)
    {
        /* ACMD41 with HCS for SD v2: wait until the card exits idle. */
        uint16_t no_resp = 0;
        for (uint16_t i = 0; i < CARD_READER_ACMD41_ATTEMPTS; i++)
        {
            uint8_t r1_55 = card_reader_spi_send_cmd(CARD_READER_SD_CMD55, 0, CARD_READER_SD_CMD_CRC_DUMMY, 0, 0);
            (void)r1_55;
            card_reader_spi_transfer_byte(CARD_READER_SD_DUMMY_BYTE);
            /* Issue ACMD41 with HCS to request SD v2 initialization. */
            r1 = card_reader_spi_send_cmd(CARD_READER_SD_ACMD41,
                                          CARD_READER_SD_ACMD41_ARG_HCS,
                                          CARD_READER_SD_CMD_CRC_DUMMY,
                                          0,
                                          0);
            card_reader_spi_deselect();
            state->regs.sd_last_acmd41_r1 = r1;
            /* Exit loop once ACMD41 reports READY. */
            if (r1 == SD_R1_STATE_READY)
            {
                /* Card is ready for normal operation. */
                break;
            }
            /* Retry CMD0/CMD8 if the card stops responding. */
            if (r1 == CARD_READER_SD_R1_NO_RESPONSE)
            {
                no_resp++;
                /* Insert idle clocks between retries to resynchronize. */
                card_reader_spi_idle_clocks(CARD_READER_SD_IDLE_CLOCKS_SHORT);
                /* After many no-responses, restart the init sequence. */
                if (no_resp == CARD_READER_SD_NO_RESP_LIMIT)
                {
                    /* Re-issue CMD0/CMD8 if repeated no-response persists. */
                    card_reader_spi_idle_clocks(CARD_READER_SD_IDLE_CLOCKS_RETRY);
                    card_reader_spi_send_cmd(CARD_READER_SD_CMD0, 0, CARD_READER_SD_CMD0_CRC, 0, 0);
                    card_reader_spi_send_cmd(CARD_READER_SD_CMD8,
                                             CARD_READER_SD_CMD8_ARG,
                                             CARD_READER_SD_CMD8_CRC,
                                             cmd8_response,
                                             0);
                    no_resp = 0;
                }
            }
            /* Small delay to avoid saturating the card with commands. */
            hal_delay_ms(CARD_READER_SD_ACMD41_DELAY_MS);
        }
        /* Fail if ACMD41 never brought the card out of idle. */
        if (r1 != SD_R1_STATE_READY)
        {
            goto out;
        }
        /* CMD58: read OCR to detect SDHC/SDXC support. */
        uint8_t ocr[4] = {0};
        r1 = card_reader_spi_send_cmd(CARD_READER_SD_CMD58, 0, CARD_READER_SD_CMD_CRC_DUMMY, ocr, 0);
        state->regs.sd_last_cmd58_r1 = r1;
        for (uint8_t i = 0; i < CARD_READER_CMD8_R7_BYTES; i++)
        {
            state->regs.sd_last_cmd58_ocr[i] = ocr[i];
        }
        /* OCR is only valid if the card is READY and power-up is complete. */
        if (r1 == SD_R1_STATE_READY && (ocr[0] & SD_OCR_POWER_UP_STATUS))
        {
            /* CCS bit indicates block-addressing (SDHC/SDXC). */
            if (ocr[0] & SD_OCR_CCS)
            {
                state->status.sd_is_sdhc = 1;
            }
        }
    }
    else
    {
        /* SD v1/MMC: use ACMD41 without HCS. */
        for (uint16_t i = 0; i < CARD_READER_ACMD41_ATTEMPTS; i++)
        {
            uint8_t r1_55 = card_reader_spi_send_cmd(CARD_READER_SD_CMD55, 0, CARD_READER_SD_CMD_CRC_DUMMY, 0, 0);
            (void)r1_55;
            card_reader_spi_transfer_byte(CARD_READER_SD_DUMMY_BYTE);
            r1 = card_reader_spi_send_cmd(CARD_READER_SD_ACMD41, 0, CARD_READER_SD_CMD_CRC_DUMMY, 0, 0);
            card_reader_spi_deselect();
            state->regs.sd_last_acmd41_r1 = r1;
            /* Exit loop once ACMD41 reports READY. */
            if (r1 == SD_R1_STATE_READY)
            {
                /* Card is ready for normal operation. */
                break;
            }
            /* Small delay to avoid saturating the card with commands. */
            hal_delay_ms(CARD_READER_SD_ACMD41_DELAY_MS);
        }
        /* If ACMD41 fails, try the MMC CMD1 fallback. */
        if (r1 != SD_R1_STATE_READY)
        {
            /* MMC fallback: CMD1 without toggling chip select. */
            card_reader_spi_deselect();
            hal_sd_cs_low();
            for (uint16_t i = 0; i < CARD_READER_CMD1_ATTEMPTS; i++)
            {
                r1 = card_reader_spi_send_cmd(CARD_READER_SD_CMD1, 0, CARD_READER_SD_CMD_CRC_DUMMY, 0, 1);
                /* Exit loop when CMD1 reports READY. */
                if (r1 == SD_R1_STATE_READY)
                {
                    /* Card is ready after MMC init sequence. */
                    break;
                }
                /* Delay between CMD1 retries. */
                hal_delay_ms(CARD_READER_SD_ACMD41_DELAY_MS);
            }
            hal_sd_cs_high();
        }
        /* Fail if neither ACMD41 nor CMD1 succeeded. */
        if (r1 != SD_R1_STATE_READY)
        {
            goto out;
        }
    }

    ok = 1;

    out:
    /* Restore fast SPI for data transfers. */
    card_reader_spi_deselect();
    card_reader_spi_set_speed(CARD_READER_SPI_SPEED_FAST);
    return ok;
}

/************************************************
* card_reader_core_open
* Allocates and initializes a card reader instance.
* Parameters: none.
* Returns: pointer to instance on success, NULL on failure.
***************************************************/
card_reader_state_t *card_reader_core_open(void)
{
    card_reader_state_t *state = (card_reader_state_t *)malloc(sizeof(*state));
    /* Abort if allocation fails. */
    if (!state)
    {
        return NULL;
    }
    /* Initialize state and attempt SD/FAT bring-up. */
    card_reader_state_reset(state);

    uint8_t sd_ok = sd_init(state);
    state->status.sd_last_fat_format_ok = sd_ok ? card_reader_fat_is_formatted(state) : 0;
    state->status.sd_last_fat_mount_ok = 0;
    /* Mount FAT only if SD init and format checks pass. */
    if (sd_ok && state->status.sd_last_fat_format_ok)
    {
        state->status.sd_last_fat_mount_ok = card_reader_fat_mount(state);
    }
    return state;
}

/************************************************
* card_reader_core_read
* Reads a file from the SD card and streams it to a sink callback.
* Parameters: dev = card reader instance, name = filename,
*             expected_size = 0 to ignore size or exact size,
*             sink = callback for data chunks, ctx = user context.
* Returns: 1 on success, 0 on failure.
***************************************************/
uint8_t card_reader_core_read(card_reader_state_t *dev,
const char *name,
uint32_t expected_size,
void (*sink)(const uint8_t *data, uint16_t len, void *ctx),
void *ctx)
{
    /* Validate inputs and ensure FAT is mounted. */
    /* Reject invalid inputs early. */
    if (!dev || !name || !sink)
    {
        return 0;
    }
    /* Refuse to read when FAT is not ready. */
    if (!dev->status.sd_fat32_ready)
    {
        return 0;
    }
    return card_reader_fat_read_file_stream_ctx(dev, name, expected_size, sink, ctx);
}

/************************************************
* card_reader_core_write
* Stub write API (not implemented).
* Parameters: dev = card reader instance, name = filename,
*             data = input data, len = data length.
* Returns: 0 (not supported).
***************************************************/
uint8_t card_reader_core_write(card_reader_state_t *dev,
const char *name,
const uint8_t *data,
uint32_t len)
{
    /* Write support not implemented in this version. */
    (void)dev;
    (void)name;
    (void)data;
    (void)len;
    return 0;
}

/************************************************
* card_reader_core_close
* Releases a card reader instance and resets hardware state.
* Parameters: dev = card reader instance.
* Returns: void.
***************************************************/
void card_reader_core_close(card_reader_state_t *dev)
{
    /* Ignore NULL to simplify caller cleanup. */
    if (!dev)
    {
        return;
    }
    /* Release hardware state and free the instance. */
    card_reader_spi_deselect();
    card_reader_state_reset(dev);
    free(dev);
}
