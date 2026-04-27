#include "apps/nfc_reader/nfc_reader.h"

#include "hal/hal.h"
#include "system/expansions_detector/expansions_devices.h"
#include "system/render/render_api.h"

#include <stddef.h>

enum {
    NFC_READER_POLL_MS = 500u,
    NFC_READER_I2C_TIMEOUT_MS = 80u,
    NFC_READER_RECOVER_FAILURES = 10u,
    NFC_READER_DISCONNECT_FAILURES = 12u,
    NFC_READER_PN532_FRAME_CAPACITY = 48u,
    NFC_READER_PN532_READY = 0x01u,
    NFC_READER_PN532_HOST_TO_PN532 = 0xD4u,
    NFC_READER_PN532_TO_HOST = 0xD5u,
    NFC_READER_PN532_CMD_SAMCONFIGURATION = 0x14u,
    NFC_READER_PN532_CMD_RFCONFIGURATION = 0x32u,
    NFC_READER_PN532_CMD_INLISTPASSIVETARGET = 0x4Au,
};

typedef enum {
    NFC_READER_TAG_ABSENT = 0,
    NFC_READER_TAG_PRESENT,
    NFC_READER_TAG_ERROR,
} nfc_reader_tag_state_t;

static uint8_t pn532_wait_ready(uint16_t timeout_ms)
{
    uint16_t waited_ms = 0;

    while (waited_ms <= timeout_ms)
    {
        uint8_t status = 0;
        if (hal_i2c_read(EXPANSION_I2C_ADDRESS_PN532, &status, 1u, NFC_READER_I2C_TIMEOUT_MS) &&
            status == NFC_READER_PN532_READY)
        {
            return 1;
        }
        hal_delay_ms(10u);
        waited_ms = (uint16_t)(waited_ms + 10u);
    }

    return 0;
}

static uint8_t pn532_write_frame(const uint8_t *payload, uint8_t payload_len)
{
    uint8_t frame[32] = {0};
    uint8_t len = (uint8_t)(payload_len + 1u);
    uint8_t sum = NFC_READER_PN532_HOST_TO_PN532;
    uint8_t i = 0;

    if (!payload || payload_len > 24u)
    {
        return 0;
    }

    frame[0] = 0x00u;
    frame[1] = 0x00u;
    frame[2] = 0xFFu;
    frame[3] = len;
    frame[4] = (uint8_t)(0u - len);
    frame[5] = NFC_READER_PN532_HOST_TO_PN532;
    for (i = 0; i < payload_len; i++)
    {
        frame[6u + i] = payload[i];
        sum = (uint8_t)(sum + payload[i]);
    }
    frame[6u + payload_len] = (uint8_t)(0u - sum);
    frame[7u + payload_len] = 0x00u;

    return hal_i2c_write(EXPANSION_I2C_ADDRESS_PN532,
                         frame,
                         (uint16_t)(8u + payload_len),
                         NFC_READER_I2C_TIMEOUT_MS);
}

static uint8_t pn532_read_ack(void)
{
    uint8_t ack[7] = {0};

    if (!pn532_wait_ready(120u))
    {
        return 0;
    }
    if (!hal_i2c_read(EXPANSION_I2C_ADDRESS_PN532, ack, sizeof(ack), NFC_READER_I2C_TIMEOUT_MS))
    {
        return 0;
    }

    return (ack[1] == 0x00u &&
            ack[2] == 0x00u &&
            ack[3] == 0xFFu &&
            ack[4] == 0x00u &&
            ack[5] == 0xFFu &&
            ack[6] == 0x00u) ? 1u : 0u;
}

static uint8_t pn532_read_response(uint8_t expected_cmd, uint8_t *out, uint8_t out_capacity, uint8_t *out_len)
{
    uint8_t frame[NFC_READER_PN532_FRAME_CAPACITY] = {0};
    uint16_t frame_read_len = 0;
    uint8_t len = 0;
    uint8_t i = 0;

    if (!out || !out_len)
    {
        return 0;
    }
    *out_len = 0;

    if (!pn532_wait_ready(1000u))
    {
        return 0;
    }

    frame_read_len = (uint16_t)(out_capacity + 8u);
    if (frame_read_len > sizeof(frame))
    {
        frame_read_len = sizeof(frame);
    }

    if (!hal_i2c_read(EXPANSION_I2C_ADDRESS_PN532, frame, frame_read_len, NFC_READER_I2C_TIMEOUT_MS))
    {
        return 0;
    }
    if (frame[1] != 0x00u || frame[2] != 0x00u || frame[3] != 0xFFu)
    {
        return 0;
    }

    len = frame[4];
    if (len < 2u || (uint8_t)(len + frame[5]) != 0u || frame[6] != NFC_READER_PN532_TO_HOST)
    {
        return 0;
    }
    if (frame[7] != (uint8_t)(expected_cmd + 1u))
    {
        return 0;
    }

    len = (uint8_t)(len - 1u);
    if (len > out_capacity)
    {
        len = out_capacity;
    }
    for (i = 0; i < len; i++)
    {
        out[i] = frame[7u + i];
    }
    *out_len = len;
    return 1;
}

static uint8_t pn532_command(const uint8_t *payload,
                             uint8_t payload_len,
                             uint8_t expected_cmd,
                             uint8_t *response,
                             uint8_t response_capacity,
                             uint8_t *response_len)
{
    if (!pn532_write_frame(payload, payload_len))
    {
        return 0;
    }
    if (!pn532_read_ack())
    {
        return 0;
    }
    return pn532_read_response(expected_cmd, response, response_capacity, response_len);
}

static uint8_t pn532_init(void)
{
    uint8_t wake = 0x00u;
    uint8_t response[8] = {0};
    uint8_t response_len = 0;
    uint8_t sam_ok = 0;
    const uint8_t sam_config[] = {
        NFC_READER_PN532_CMD_SAMCONFIGURATION,
        0x01u,
        0x14u,
        0x01u,
    };
    const uint8_t passive_retries[] = {
        NFC_READER_PN532_CMD_RFCONFIGURATION,
        0x05u,
        0xFFu,
        0x01u,
        0x02u,
    };

    (void)hal_i2c_write(EXPANSION_I2C_ADDRESS_PN532, &wake, 1u, NFC_READER_I2C_TIMEOUT_MS);
    hal_delay_ms(20u);

    sam_ok = pn532_command(sam_config,
                           sizeof(sam_config),
                           NFC_READER_PN532_CMD_SAMCONFIGURATION,
                           response,
                           sizeof(response),
                           &response_len);
    if (!sam_ok)
    {
        return 0;
    }

    return pn532_command(passive_retries,
                         sizeof(passive_retries),
                         NFC_READER_PN532_CMD_RFCONFIGURATION,
                         response,
                         sizeof(response),
                         &response_len);
}

static nfc_reader_tag_state_t pn532_tag_state(void)
{
    uint8_t response[24] = {0};
    uint8_t response_len = 0;
    const uint8_t command[] = {
        NFC_READER_PN532_CMD_INLISTPASSIVETARGET,
        0x01u,
        0x00u,
    };

    if (!pn532_command(command,
                      sizeof(command),
                      NFC_READER_PN532_CMD_INLISTPASSIVETARGET,
                      response,
                      sizeof(response),
                      &response_len))
    {
        return hal_i2c_probe_address(EXPANSION_I2C_ADDRESS_PN532) ? NFC_READER_TAG_ABSENT : NFC_READER_TAG_ERROR;
    }

    return (response_len >= 2u && response[1] >= 1u) ? NFC_READER_TAG_PRESENT : NFC_READER_TAG_ABSENT;
}

uint8_t nfc_reader_app_task(void *ctx)
{
    nfc_reader_tag_state_t last_tag_state = NFC_READER_TAG_ERROR;
    uint8_t failure_count = 0;

    (void)ctx;

    if (!pn532_init())
    {
        (void)render_show_text_screen("NFC INIT FAIL", "PN532", "CHECK MODULE", NULL);
        return 0;
    }

    (void)render_show_text_screen("NFC READY", "NO TAG", "SCAN TAG", NULL);

    for (;;)
    {
        nfc_reader_tag_state_t tag_state = pn532_tag_state();

        if (tag_state == NFC_READER_TAG_ERROR)
        {
            if (failure_count < NFC_READER_DISCONNECT_FAILURES)
            {
                failure_count++;
            }

            if (failure_count == NFC_READER_RECOVER_FAILURES)
            {
                (void)hal_i2c_recover();
                (void)pn532_init();
                last_tag_state = NFC_READER_TAG_ERROR;
            }
            else if (failure_count >= NFC_READER_DISCONNECT_FAILURES)
            {
                (void)render_show_text_screen("NFC LOST", "CHECK MODULE", NULL, NULL);
                return 0;
            }

            hal_delay_ms(NFC_READER_POLL_MS);
            continue;
        }

        failure_count = 0;
        if (tag_state != last_tag_state)
        {
            if (tag_state == NFC_READER_TAG_PRESENT)
            {
                (void)render_show_text_screen("NFC TAG", "PRESENT", NULL, NULL);
            }
            else
            {
                (void)render_show_text_screen("NFC READY", "NO TAG", "SCAN TAG", NULL);
            }
            last_tag_state = tag_state;
        }

        hal_delay_ms(NFC_READER_POLL_MS);
    }

    return 1;
}
