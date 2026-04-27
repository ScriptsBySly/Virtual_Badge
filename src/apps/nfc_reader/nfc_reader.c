#include "apps/nfc_reader/nfc_reader.h"

#include "hal/hal.h"
#include "system/expansions_detector/expansions_devices.h"
#include "system/render/render_api.h"

#include <stddef.h>
#include <string.h>

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
    NFC_READER_PN532_CMD_INDATAEXCHANGE = 0x40u,
    NFC_READER_MIFARE_CMD_READ = 0x30u,
    NFC_READER_TEXT_CAPACITY = 64u,
    NFC_READER_TAG_DATA_CAPACITY = 160u,
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

static uint8_t pn532_read_type2_pages(uint8_t start_page, uint8_t *out16)
{
    uint8_t response[20] = {0};
    uint8_t response_len = 0;
    const uint8_t command[] = {
        NFC_READER_PN532_CMD_INDATAEXCHANGE,
        0x01u,
        NFC_READER_MIFARE_CMD_READ,
        start_page,
    };

    if (!out16)
    {
        return 0;
    }

    if (!pn532_command(command,
                      sizeof(command),
                      NFC_READER_PN532_CMD_INDATAEXCHANGE,
                      response,
                      sizeof(response),
                      &response_len))
    {
        return 0;
    }
    if (response_len < 18u || response[1] != 0x00u)
    {
        return 0;
    }

    memcpy(out16, &response[2], 16u);
    return 1;
}

static uint8_t nfc_reader_ndef_tlv_is_complete(const uint8_t *data, uint16_t data_len)
{
    uint16_t i = 0;

    if (!data)
    {
        return 0;
    }

    while (i < data_len)
    {
        uint8_t tlv = data[i++];
        uint16_t len = 0;

        if (tlv == 0x00u)
        {
            continue;
        }
        if (tlv == 0xFEu)
        {
            return 1;
        }
        if (i >= data_len)
        {
            return 0;
        }

        len = data[i++];
        if (len == 0xFFu)
        {
            if (i + 2u > data_len)
            {
                return 0;
            }
            len = (uint16_t)(((uint16_t)data[i] << 8) | data[i + 1u]);
            i = (uint16_t)(i + 2u);
        }

        if (tlv == 0x03u)
        {
            return (uint16_t)(i + len) <= data_len ? 1u : 0u;
        }

        i = (uint16_t)(i + len);
    }

    return 0;
}

static uint16_t pn532_read_type2_data(uint8_t *out, uint16_t capacity)
{
    uint16_t offset = 0;
    uint8_t page = 4u;

    if (!out)
    {
        return 0;
    }

    while ((uint16_t)(offset + 16u) <= capacity)
    {
        if (!pn532_read_type2_pages(page, &out[offset]))
        {
            break;
        }
        offset = (uint16_t)(offset + 16u);
        if (nfc_reader_ndef_tlv_is_complete(out, offset))
        {
            break;
        }
        page = (uint8_t)(page + 4u);
    }

    return offset;
}

static uint8_t nfc_reader_copy_text_record(const uint8_t *ndef,
                                           uint16_t ndef_len,
                                           char *out,
                                           uint8_t out_capacity)
{
    uint16_t i = 0;

    if (!ndef || !out || out_capacity == 0u)
    {
        return 0;
    }
    out[0] = '\0';

    while (i + 3u < ndef_len)
    {
        uint8_t flags = ndef[i++];
        uint8_t type_len = ndef[i++];
        uint32_t payload_len = 0;
        uint8_t id_len = 0;
        uint16_t type_index = 0;
        uint16_t payload_index = 0;
        uint8_t lang_len = 0;
        uint16_t text_len = 0;

        if (flags & 0x10u)
        {
            payload_len = ndef[i++];
        }
        else
        {
            if (i + 4u > ndef_len)
            {
                return 0;
            }
            payload_len = ((uint32_t)ndef[i] << 24) |
                          ((uint32_t)ndef[i + 1u] << 16) |
                          ((uint32_t)ndef[i + 2u] << 8) |
                          (uint32_t)ndef[i + 3u];
            i = (uint16_t)(i + 4u);
        }

        if (flags & 0x08u)
        {
            if (i >= ndef_len)
            {
                return 0;
            }
            id_len = ndef[i++];
        }
        if ((uint32_t)i + type_len + id_len + payload_len > ndef_len)
        {
            return 0;
        }

        type_index = i;
        payload_index = (uint16_t)(type_index + type_len + id_len);
        if ((flags & 0x07u) == 0x01u && type_len == 1u && ndef[type_index] == 'T' && payload_len >= 1u)
        {
            lang_len = (uint8_t)(ndef[payload_index] & 0x3Fu);
            if (payload_len <= (uint32_t)(1u + lang_len))
            {
                return 0;
            }
            payload_index = (uint16_t)(payload_index + 1u + lang_len);
            text_len = (uint16_t)(payload_len - 1u - lang_len);
            if (text_len >= out_capacity)
            {
                text_len = (uint16_t)(out_capacity - 1u);
            }
            memcpy(out, &ndef[payload_index], text_len);
            out[text_len] = '\0';
            return 1;
        }

        i = (uint16_t)(type_index + type_len + id_len + payload_len);
        if (flags & 0x40u)
        {
            break;
        }
    }

    return 0;
}

static uint8_t nfc_reader_scan_text_record(const uint8_t *data,
                                           uint16_t data_len,
                                           char *out,
                                           uint8_t out_capacity)
{
    uint16_t start = 0;

    if (!data || !out || out_capacity == 0u)
    {
        return 0;
    }

    for (start = 0; start + 5u < data_len; start++)
    {
        uint8_t flags = data[start];
        uint8_t type_len = data[start + 1u];
        uint16_t payload_len_index = (uint16_t)(start + 2u);
        uint32_t payload_len = 0;
        uint16_t type_index = 0;
        uint16_t payload_index = 0;
        uint8_t lang_len = 0;
        uint16_t text_len = 0;

        if ((flags & 0x07u) != 0x01u || type_len != 1u)
        {
            continue;
        }

        if (flags & 0x10u)
        {
            payload_len = data[payload_len_index];
            type_index = (uint16_t)(payload_len_index + 1u);
        }
        else
        {
            if (payload_len_index + 4u > data_len)
            {
                continue;
            }
            payload_len = ((uint32_t)data[payload_len_index] << 24) |
                          ((uint32_t)data[payload_len_index + 1u] << 16) |
                          ((uint32_t)data[payload_len_index + 2u] << 8) |
                          (uint32_t)data[payload_len_index + 3u];
            type_index = (uint16_t)(payload_len_index + 4u);
        }

        if ((uint32_t)type_index + type_len + payload_len > data_len)
        {
            continue;
        }
        if (data[type_index] != 'T' || payload_len < 1u)
        {
            continue;
        }

        payload_index = (uint16_t)(type_index + type_len);
        lang_len = (uint8_t)(data[payload_index] & 0x3Fu);
        if (payload_len <= (uint32_t)(1u + lang_len))
        {
            continue;
        }

        payload_index = (uint16_t)(payload_index + 1u + lang_len);
        text_len = (uint16_t)(payload_len - 1u - lang_len);
        if (text_len >= out_capacity)
        {
            text_len = (uint16_t)(out_capacity - 1u);
        }

        memcpy(out, &data[payload_index], text_len);
        out[text_len] = '\0';
        return 1;
    }

    return 0;
}

static uint8_t nfc_reader_extract_text(const uint8_t *data,
                                       uint16_t data_len,
                                       char *out,
                                       uint8_t out_capacity)
{
    uint16_t i = 0;

    if (!data || !out || out_capacity == 0u)
    {
        return 0;
    }
    out[0] = '\0';

    while (i < data_len)
    {
        uint8_t tlv = data[i++];
        uint16_t len = 0;

        if (tlv == 0x00u)
        {
            continue;
        }
        if (tlv == 0xFEu)
        {
            return 0;
        }
        if (i >= data_len)
        {
            return 0;
        }

        len = data[i++];
        if (len == 0xFFu)
        {
            if (i + 2u > data_len)
            {
                return 0;
            }
            len = (uint16_t)(((uint16_t)data[i] << 8) | data[i + 1u]);
            i = (uint16_t)(i + 2u);
        }
        if (i + len > data_len)
        {
            return 0;
        }
        if (tlv == 0x03u)
        {
            return nfc_reader_copy_text_record(&data[i], len, out, out_capacity);
        }
        i = (uint16_t)(i + len);
    }

    return nfc_reader_scan_text_record(data, data_len, out, out_capacity);
}

static uint8_t nfc_reader_read_text(char *out, uint8_t out_capacity)
{
    uint8_t data[NFC_READER_TAG_DATA_CAPACITY] = {0};
    uint16_t data_len = pn532_read_type2_data(data, sizeof(data));

    if (data_len == 0u)
    {
        return 0;
    }

    if (nfc_reader_extract_text(data, data_len, out, out_capacity))
    {
        return 1;
    }

    return nfc_reader_scan_text_record(data, data_len, out, out_capacity);
}

uint8_t nfc_reader_app_task(void *ctx)
{
    nfc_reader_tag_state_t last_tag_state = NFC_READER_TAG_ERROR;
    char last_text[NFC_READER_TEXT_CAPACITY] = {0};
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
                char text[NFC_READER_TEXT_CAPACITY] = {0};

                if (nfc_reader_read_text(text, sizeof(text)))
                {
                    (void)render_show_text_screen("NFC TEXT", text, NULL, NULL);
                    memcpy(last_text, text, sizeof(last_text));
                }
                else
                {
                    (void)render_show_text_screen("NFC TAG", "NO TEXT", NULL, NULL);
                    last_text[0] = '\0';
                }
            }
            else
            {
                (void)render_show_text_screen("NFC READY", "NO TAG", "SCAN TAG", NULL);
                last_text[0] = '\0';
            }
            last_tag_state = tag_state;
        }
        else if (tag_state == NFC_READER_TAG_PRESENT)
        {
            char text[NFC_READER_TEXT_CAPACITY] = {0};

            if (nfc_reader_read_text(text, sizeof(text)) && memcmp(last_text, text, sizeof(last_text)) != 0)
            {
                (void)render_show_text_screen("NFC TEXT", text, NULL, NULL);
                memcpy(last_text, text, sizeof(last_text));
            }
        }

        hal_delay_ms(NFC_READER_POLL_MS);
    }

    return 1;
}
