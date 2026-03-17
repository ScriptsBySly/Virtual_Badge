#pragma once

#include <stdint.h>

typedef struct {
    uint8_t sd_ok;
    uint8_t fat_ok;
} card_reader_status_t;

void card_reader_init(card_reader_status_t *status);
void card_reader_print_status(const card_reader_status_t *status);
void card_reader_handle_cli(const card_reader_status_t *status,
                            const char *image1,
                            const char *image2,
                            uint16_t width,
                            uint16_t height);
uint8_t card_reader_draw_raw565(const char *name, uint16_t width, uint16_t height);

uint8_t card_reader_sd_is_sdhc(void);
uint8_t card_reader_fat_ready(void);
uint32_t card_reader_fat_lba_start(void);
uint32_t card_reader_fat_first_fat(void);
uint32_t card_reader_fat_first_data(void);
uint32_t card_reader_fat_root_cluster(void);
uint8_t card_reader_fat_sectors_per_cluster(void);
uint32_t card_reader_fat_size(void);

uint8_t card_reader_sd_cmd0_r1(void);
uint8_t card_reader_sd_cmd8_r1(void);
uint8_t card_reader_sd_acmd41_r1(void);
uint8_t card_reader_sd_cmd58_r1(void);
const uint8_t *card_reader_sd_cmd8_r7(void);
const uint8_t *card_reader_sd_cmd58_ocr(void);
uint8_t card_reader_sd_fat_format_ok(void);
uint8_t card_reader_sd_fat_mount_ok(void);
