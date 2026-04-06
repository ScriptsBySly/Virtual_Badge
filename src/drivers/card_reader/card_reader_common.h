#pragma once

#include <stdint.h>

#define CARD_READER_STATUS_LINE_CAPACITY 32u

typedef void (*card_reader_wait_status_fn_t)(const char *line0,
                                             const char *line1,
                                             const char *line2,
                                             const char *line3,
                                             void *ctx);

typedef struct card_reader_state {
    struct {
        uint8_t sd_is_sdhc;
        uint8_t sd_fat32_ready;
        uint8_t sd_last_fat_format_ok;
        uint8_t sd_last_fat_mount_ok;
    } status;
    struct {
        uint8_t sd_last_cmd0_r1;
        uint8_t sd_last_cmd8_r1;
        uint8_t sd_last_acmd41_r1;
        uint8_t sd_last_cmd58_r1;
        uint8_t sd_last_cmd8_r7[4];
        uint8_t sd_last_cmd58_ocr[4];
    } regs;
    struct {
        uint32_t lba_start;
        uint32_t first_fat;
        uint32_t first_data;
        uint32_t fat_size;
        uint8_t sectors_per_cluster;
        uint32_t root_cluster;
    } fat32;
} card_reader_state_t;
