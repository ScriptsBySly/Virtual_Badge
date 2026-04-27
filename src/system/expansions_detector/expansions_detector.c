#include "system/expansions_detector/expansions_detector.h"

#include "hal/hal.h"
#include "system/expansions_detector/expansions_devices.h"
#include "system/render/render_api.h"

#include <stdio.h>

enum {
    EXPANSIONS_DETECTOR_POLL_MS = 500u,
    EXPANSIONS_DETECTOR_NO_DEVICE_INDEX = 0xFFu,
    EXPANSIONS_DETECTOR_NO_ADDRESS = 0xFFu,
};

typedef struct {
    uint8_t found;
    uint8_t device_index;
    uint8_t address;
    uint8_t timeout_seen;
    uint8_t error_seen;
} expansions_detector_scan_t;

static expansions_detector_scan_t expansions_detector_scan_first(void)
{
    expansions_detector_scan_t scan = {
        .found = 0,
        .device_index = EXPANSIONS_DETECTOR_NO_DEVICE_INDEX,
        .address = EXPANSIONS_DETECTOR_NO_ADDRESS,
        .timeout_seen = 0,
        .error_seen = 0,
    };
    uint8_t i = 0;

    for (i = 0; i < EXPANSION_DEVICE_COUNT; i++)
    {
        const expansion_device_t *device = &k_expansion_devices[i];
        hal_i2c_probe_result_t result = HAL_I2C_PROBE_ERROR;

        if (device->bus != EXPANSION_DEVICE_BUS_I2C)
        {
            continue;
        }

        result = hal_i2c_probe_address_status(device->i2c_address);
        if (result == HAL_I2C_PROBE_FOUND)
        {
            scan.found = 1;
            scan.device_index = i;
            scan.address = device->i2c_address;
            return scan;
        }
        if (result == HAL_I2C_PROBE_TIMEOUT)
        {
            scan.timeout_seen = 1;
        }
        else if (result == HAL_I2C_PROBE_ERROR)
        {
            scan.error_seen = 1;
        }
    }

    return scan;
}

static void expansions_detector_show_status(const expansions_detector_scan_t *scan)
{
    char address_line[18] = {0};
    const expansion_device_t *device = 0;

    if (!scan)
    {
        return;
    }

    if (scan->found)
    {
        (void)snprintf(address_line, sizeof(address_line), "ADDR 0x%02X", (unsigned)scan->address);
        if (scan->device_index < EXPANSION_DEVICE_COUNT)
        {
            device = &k_expansion_devices[scan->device_index];
        }
        if (device)
        {
            (void)render_show_text_screen(device->screen_title, address_line, device->screen_detail, NULL);
            return;
        }
        (void)render_show_text_screen("I2C DEVICE", address_line, "I2C OK", NULL);
        return;
    }

    if (scan->timeout_seen)
    {
        uint8_t sda_level = 0;
        uint8_t scl_level = 0;
        char line_state[18] = {0};

        hal_i2c_get_line_levels(&sda_level, &scl_level);
        (void)snprintf(line_state,
                       sizeof(line_state),
                       "SDA %u SCL %u",
                       (unsigned)sda_level,
                       (unsigned)scl_level);
        (void)render_show_text_screen("I2C TIMEOUT", line_state, "CHECK PULLUPS", NULL);
        return;
    }

    if (scan->error_seen)
    {
        (void)render_show_text_screen("I2C ERROR", "CHECK DRIVER", "OR PINS", NULL);
        return;
    }

    (void)render_show_text_screen("EXPANSIONS", "NO DEVICE", "KNOWN LIST", NULL);
}

uint8_t expansions_detector_app_task(void *ctx)
{
    uint8_t last_found = 0xFFu;
    uint8_t last_device_index = EXPANSIONS_DETECTOR_NO_DEVICE_INDEX;
    uint8_t last_address = EXPANSIONS_DETECTOR_NO_ADDRESS;
    uint8_t last_timeout_seen = 0xFFu;
    uint8_t last_error_seen = 0xFFu;

    (void)ctx;

    if (!hal_i2c_init())
    {
        (void)render_show_text_screen("I2C INIT FAIL", "CHECK PINS", NULL, NULL);
        return 0;
    }

    (void)render_show_text_screen("I2C SCAN", "STARTING", "POLL 500 MS", NULL);

    for (;;)
    {
        expansions_detector_scan_t scan = expansions_detector_scan_first();

        if (scan.found != last_found ||
            scan.device_index != last_device_index ||
            scan.address != last_address ||
            scan.timeout_seen != last_timeout_seen ||
            scan.error_seen != last_error_seen)
        {
            expansions_detector_show_status(&scan);
            last_found = scan.found;
            last_device_index = scan.device_index;
            last_address = scan.address;
            last_timeout_seen = scan.timeout_seen;
            last_error_seen = scan.error_seen;
        }

        hal_delay_ms(EXPANSIONS_DETECTOR_POLL_MS);
    }

    return 1;
}
