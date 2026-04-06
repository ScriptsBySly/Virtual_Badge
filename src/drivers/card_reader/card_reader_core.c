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
#define CARD_READER_WAIT_RETRY_DELAY_MS 500
#define CARD_READER_WAIT_BOOT_DELAY_MS 50
#define CARD_READER_HEX_DIGITS 8
#define CARD_READER_BITS_PER_NIBBLE 4
#define CARD_READER_DECIMAL_BASE 10u
#define CARD_READER_DECIMAL_HUNDREDS 100u
#define CARD_READER_CMD8_VOLTAGE_INDEX 2
#define CARD_READER_CMD8_PATTERN_INDEX 3
#define CARD_READER_OCR_STATUS_INDEX 0

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
* card_reader_append_str
* Appends a source string to the destination cursor.
* Parameters: dst = destination cursor, src = source string.
* Returns: updated destination cursor after the copied text.
***************************************************/
static char *card_reader_append_str(char *dst, const char *src)
{
    while (*src)
    {
        *dst++ = *src++;
    }
    return dst;
}

/************************************************
* card_reader_append_hex32
* Appends a 32-bit value as hexadecimal text with a 0x prefix.
* Parameters: dst = destination cursor, value = value to format.
* Returns: updated destination cursor after the formatted value.
***************************************************/
static char *card_reader_append_hex32(char *dst, uint32_t value)
{
    static const char hex[] = "0123456789ABCDEF";

    *dst++ = '0';
    *dst++ = 'x';
    for (int8_t i = (CARD_READER_HEX_DIGITS - 1); i >= 0; i--)
    {
        uint8_t nib = (uint8_t)((value >> (uint8_t)(i * CARD_READER_BITS_PER_NIBBLE)) & 0x0Fu);
        *dst++ = hex[nib];
    }
    return dst;
}

/************************************************
* card_reader_append_u8_dec
* Appends an 8-bit value as decimal ASCII text.
* Parameters: dst = destination cursor, value = value to format.
* Returns: updated destination cursor after the formatted value.
***************************************************/
static char *card_reader_append_u8_dec(char *dst, uint8_t value)
{
    if (value >= CARD_READER_DECIMAL_HUNDREDS)
    {
        *dst++ = (char)('0' + (value / CARD_READER_DECIMAL_HUNDREDS));
        value = (uint8_t)(value % CARD_READER_DECIMAL_HUNDREDS);
        *dst++ = (char)('0' + (value / CARD_READER_DECIMAL_BASE));
        *dst++ = (char)('0' + (value % CARD_READER_DECIMAL_BASE));
        return dst;
    }
    if (value >= CARD_READER_DECIMAL_BASE)
    {
        *dst++ = (char)('0' + (value / CARD_READER_DECIMAL_BASE));
        *dst++ = (char)('0' + (value % CARD_READER_DECIMAL_BASE));
        return dst;
    }
    *dst++ = (char)('0' + value);
    return dst;
}

/************************************************
* card_reader_format_wait_status
* Builds human-readable status lines for the SD wait-and-retry screen.
* Parameters: dev = current card reader instance, attempt = retry count,
*             line0-line3 = output buffers.
* Returns: void.
***************************************************/
static void card_reader_format_wait_status(const card_reader_state_t *dev,
                                           uint8_t attempt,
                                           char *line0,
                                           char *line1,
                                           char *line2,
                                           char *line3)
{
    char *p = 0;

    /* Start with the generic wait heading and a retry counter line. */
    if (line0)
    {
        strcpy(line0, "WAIT SD");
    }
    if (line1)
    {
        p = line1;
        p = card_reader_append_str(p, "TRY:");
        p = card_reader_append_u8_dec(p, attempt);
        *p = '\0';
    }
    if (line2)
    {
        line2[0] = '\0';
    }
    if (line3)
    {
        line3[0] = '\0';
    }
    /* Without a device instance there are no SD register values to report yet. */
    if (!dev)
    {
        return;
    }
    /* When a failed probe produced state, include the last command responses for debugging. */
    if (line2)
    {
        p = line2;
        p = card_reader_append_str(p, "C0:");
        p = card_reader_append_hex32(p, dev->regs.sd_last_cmd0_r1);
        *p = '\0';
    }
    if (line3)
    {
        p = line3;
        p = card_reader_append_str(p, "C8:");
        p = card_reader_append_hex32(p, dev->regs.sd_last_cmd8_r1);
        *p = '\0';
    }
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
        cmd8_response[CARD_READER_CMD8_VOLTAGE_INDEX] == SD_CMD8_VOLTAGE_27_36 &&
        cmd8_response[CARD_READER_CMD8_PATTERN_INDEX] == SD_CMD8_CHECK_PATTERN)
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
        if (r1 == SD_R1_STATE_READY && (ocr[CARD_READER_OCR_STATUS_INDEX] & SD_OCR_POWER_UP_STATUS))
        {
            /* CCS bit indicates block-addressing (SDHC/SDXC). */
            if (ocr[CARD_READER_OCR_STATUS_INDEX] & SD_OCR_CCS)
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
* card_reader_core_wait_ready
* Repeatedly initializes the SD card until FAT is ready.
* Parameters: status_fn = optional retry-status callback, ctx = callback context.
* Returns: ready card reader instance on success.
***************************************************/
card_reader_state_t *card_reader_core_wait_ready(card_reader_wait_status_fn_t status_fn, void *ctx)
{
    card_reader_state_t *state = 0;
    uint8_t attempt = 0;

    /* Keep retrying until SD init and FAT mount both report ready. */
    while (!state || !state->status.sd_fat32_ready)
    {
        /* Drop any previous failed instance before starting another probe cycle. */
        if (state)
        {
            card_reader_core_close(state);
            state = 0;
        }

        hal_spi_sd_init();
        hal_spi_sd_set_speed_very_slow();
        hal_sd_cs_high();
        hal_delay_ms(CARD_READER_WAIT_BOOT_DELAY_MS);

        /* Run a fresh open attempt after the hardware has been re-primed. */
        state = card_reader_core_open();
        if (state && state->status.sd_fat32_ready)
        {
            break;
        }

        /* Report retry progress to the caller when a wait-status callback is provided. */
        if (status_fn)
        {
            char line0[CARD_READER_STATUS_LINE_CAPACITY] = {0};
            char line1[CARD_READER_STATUS_LINE_CAPACITY] = {0};
            char line2[CARD_READER_STATUS_LINE_CAPACITY] = {0};
            char line3[CARD_READER_STATUS_LINE_CAPACITY] = {0};

            card_reader_format_wait_status(state, attempt, line0, line1, line2, line3);
            status_fn(line0, line1, line2, line3, ctx);
        }

        /* Back off briefly before the next initialization attempt. */
        attempt++;
        hal_delay_ms(CARD_READER_WAIT_RETRY_DELAY_MS);
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

/************************************************
* card_reader_core_describe_status
* Formats SD status lines for display or logging.
* Parameters: dev = card reader instance, line0-line3 = output buffers.
* Returns: void.
***************************************************/
void card_reader_core_describe_status(const card_reader_state_t *dev,
char *line0,
char *line1,
char *line2,
char *line3)
{
    char *p = 0;

    /* Clear the caller buffers up front so omitted fields do not contain stale text. */
    if (line0)
    {
        strcpy(line0, "SD STATUS");
    }
    if (line1)
    {
        line1[0] = '\0';
    }
    if (line2)
    {
        line2[0] = '\0';
    }
    if (line3)
    {
        line3[0] = '\0';
    }

    /* If there is no device, report a simple failure banner and stop there. */
    if (!dev)
    {
        if (line1)
        {
            strcpy(line1, "SD:FAIL");
        }
        return;
    }

    /* Format the high-level SD/FAT readiness summary first. */
    if (line1)
    {
        p = line1;
        p = card_reader_append_str(p, "SDHC:");
        p = card_reader_append_u8_dec(p, dev->status.sd_is_sdhc);
        p = card_reader_append_str(p, " FAT:");
        p = card_reader_append_u8_dec(p, dev->status.sd_fat32_ready);
        *p = '\0';
    }
    /* Include the last low-level command responses for debugging visibility. */
    if (line2)
    {
        p = line2;
        p = card_reader_append_str(p, "C0:");
        p = card_reader_append_hex32(p, dev->regs.sd_last_cmd0_r1);
        *p = '\0';
    }
    if (line3)
    {
        p = line3;
        p = card_reader_append_str(p, "C8:");
        p = card_reader_append_hex32(p, dev->regs.sd_last_cmd8_r1);
        *p = '\0';
    }
}
