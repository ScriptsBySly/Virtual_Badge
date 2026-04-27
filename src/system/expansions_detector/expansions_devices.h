#pragma once

#include <stdint.h>

typedef enum {
    EXPANSION_DEVICE_BUS_I2C = 0,
} expansion_device_bus_t;

typedef enum {
    EXPANSION_DEVICE_KIND_NFC = 0,
} expansion_device_kind_t;

typedef struct {
    const char *name;
    const char *screen_title;
    const char *screen_detail;
    expansion_device_bus_t bus;
    expansion_device_kind_t kind;
    uint8_t i2c_address;
} expansion_device_t;

enum {
    EXPANSION_I2C_ADDRESS_PN532 = 0x24u,
};

static const expansion_device_t k_expansion_devices[] = {
    {
        .name = "PN532",
        .screen_title = "PN532 FOUND",
        .screen_detail = "NFC I2C",
        .bus = EXPANSION_DEVICE_BUS_I2C,
        .kind = EXPANSION_DEVICE_KIND_NFC,
        .i2c_address = EXPANSION_I2C_ADDRESS_PN532,
    },
};

enum {
    EXPANSION_DEVICE_COUNT = (uint8_t)(sizeof(k_expansion_devices) / sizeof(k_expansion_devices[0])),
};
