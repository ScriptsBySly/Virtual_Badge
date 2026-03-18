#include "card_reader.h"

#include "display.h"
#include "hal/hal.h"

#include <stdlib.h>
#include <string.h>

static uint8_t sd_is_sdhc = 0;
static uint8_t sd_fat32_ready = 0;
static uint8_t sd_last_cmd0_r1 = 0xFF;
static uint8_t sd_last_cmd8_r1 = 0xFF;
static uint8_t sd_last_acmd41_r1 = 0xFF;
static uint8_t sd_last_cmd58_r1 = 0xFF;
static uint8_t sd_last_cmd8_r7[4] = {0};
static uint8_t sd_last_cmd58_ocr[4] = {0};
static uint8_t sd_last_fat_format_ok = 0;
static uint8_t sd_last_fat_mount_ok = 0;

#if defined(ESP_PLATFORM)
#define IMAGE_CACHE_ENABLE 1
#define IMAGE_CACHE_SLOTS 3
#else
#define IMAGE_CACHE_ENABLE 0
#define IMAGE_CACHE_SLOTS 0
#endif

typedef struct {
    uint8_t valid;
    char name[13];
    uint8_t *data;
    uint32_t size;
    uint32_t last_use;
} image_cache_entry_t;

static image_cache_entry_t image_cache[IMAGE_CACHE_SLOTS];
static uint32_t image_cache_tick = 0;


static uint32_t read_le32(const uint8_t *p) {
    return (uint32_t)p[0] |
           ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static uint32_t read_le64_low32(const uint8_t *p) {
    return read_le32(p);
}

#if IMAGE_CACHE_ENABLE
static image_cache_entry_t *cache_find(const char *name) {
    for (uint8_t i = 0; i < IMAGE_CACHE_SLOTS; i++) {
        if (image_cache[i].valid && strcmp(image_cache[i].name, name) == 0) {
            image_cache[i].last_use = ++image_cache_tick;
            return &image_cache[i];
        }
    }
    return NULL;
}

static image_cache_entry_t *cache_reserve(const char *name, uint32_t size) {
    image_cache_entry_t *slot = NULL;
    for (uint8_t i = 0; i < IMAGE_CACHE_SLOTS; i++) {
        if (!image_cache[i].valid) {
            slot = &image_cache[i];
            break;
        }
    }
    if (!slot) {
        uint32_t best = image_cache[0].last_use;
        uint8_t best_i = 0;
        for (uint8_t i = 1; i < IMAGE_CACHE_SLOTS; i++) {
            if (image_cache[i].last_use < best) {
                best = image_cache[i].last_use;
                best_i = i;
            }
        }
        slot = &image_cache[best_i];
    }
    if (slot->valid && slot->data) {
        free(slot->data);
    }
    slot->data = (uint8_t *)malloc(size);
    if (!slot->data) {
        slot->valid = 0;
        return NULL;
    }
    strncpy(slot->name, name, sizeof(slot->name) - 1);
    slot->name[sizeof(slot->name) - 1] = '\0';
    slot->size = size;
    slot->valid = 1;
    slot->last_use = ++image_cache_tick;
    return slot;
}
#endif
static uint32_t fat32_lba_start = 0;
static uint32_t fat32_first_fat = 0;
static uint32_t fat32_first_data = 0;
static uint32_t fat32_fat_size = 0;
static uint8_t fat32_sectors_per_cluster = 0;
static uint32_t fat32_root_cluster = 0;

static void sd_deselect(void) {
    hal_tft_cs_high();
    hal_sd_cs_high();
    hal_spi_sd_transfer(0xFF);
}

static void sd_idle_clocks(uint8_t count) {
    hal_tft_cs_high();
    hal_sd_cs_high();
    for (uint8_t i = 0; i < count; i++) {
        hal_spi_sd_transfer(0xFF);
    }
}

static uint8_t sd_select(void) {
    hal_tft_cs_high();
    hal_sd_cs_low();
    hal_spi_sd_transfer(0xFF);
    return 1;
}

static uint8_t sd_wait_ready(uint16_t timeout_ms) {
    while (timeout_ms--) {
        if (hal_spi_sd_transfer(0xFF) == 0xFF) {
            return 1;
        }
        hal_delay_ms(1);
    }
    return 0;
}

static uint8_t sd_send_cmd(uint8_t cmd, uint32_t arg, uint8_t crc, uint8_t *r7) {
    sd_deselect();
    sd_select();
    if (!sd_wait_ready(250)) {
        return 0xFF;
    }

    hal_spi_sd_transfer(0x40 | cmd);
    hal_spi_sd_transfer((uint8_t)(arg >> 24));
    hal_spi_sd_transfer((uint8_t)(arg >> 16));
    hal_spi_sd_transfer((uint8_t)(arg >> 8));
    hal_spi_sd_transfer((uint8_t)(arg));
    hal_spi_sd_transfer(crc);

    uint8_t r1 = 0xFF;
    for (uint8_t i = 0; i < 100; i++) {
        r1 = hal_spi_sd_transfer(0xFF);
        if ((r1 & 0x80) == 0) {
            break;
        }
    }

    if (r7) {
        for (uint8_t i = 0; i < 4; i++) {
            r7[i] = hal_spi_sd_transfer(0xFF);
        }
    }
    return r1;
}

static uint8_t sd_send_cmd_noselect(uint8_t cmd, uint32_t arg, uint8_t crc, uint8_t *r7) {
    if (!sd_wait_ready(250)) {
        return 0xFF;
    }

    hal_spi_sd_transfer(0x40 | cmd);
    hal_spi_sd_transfer((uint8_t)(arg >> 24));
    hal_spi_sd_transfer((uint8_t)(arg >> 16));
    hal_spi_sd_transfer((uint8_t)(arg >> 8));
    hal_spi_sd_transfer((uint8_t)(arg));
    hal_spi_sd_transfer(crc);

    uint8_t r1 = 0xFF;
    for (uint8_t i = 0; i < 100; i++) {
        r1 = hal_spi_sd_transfer(0xFF);
        if ((r1 & 0x80) == 0) {
            break;
        }
    }

    if (r7) {
        for (uint8_t i = 0; i < 4; i++) {
            r7[i] = hal_spi_sd_transfer(0xFF);
        }
    }
    return r1;
}

static uint8_t sd_init(void) {
    hal_sd_cs_high();
    hal_spi_sd_set_speed_very_slow();
    uint8_t ok = 0;

    hal_delay_ms(500);

    // Send 160 clocks with CS high.
    sd_idle_clocks(20);

    uint8_t r7[4] = {0};
    uint8_t r1 = 0xFF;
    for (uint8_t attempt = 0; attempt < 10; attempt++) {
        r1 = sd_send_cmd(0, 0, 0x95, 0);
        if (r1 == 0x01) {
            break;
        }
        hal_delay_ms(10);
    }
    sd_last_cmd0_r1 = r1;
    hal_uart_puts("SD CMD0 r1=");
    hal_uart_put_hex8(r1);
    hal_uart_puts("\r\n");
    if (r1 != 0x01) {
        goto out;
    }

    r1 = sd_send_cmd(8, 0x000001AA, 0x87, r7);
    sd_last_cmd8_r1 = r1;
    for (uint8_t i = 0; i < 4; i++) {
        sd_last_cmd8_r7[i] = r7[i];
    }
    hal_uart_puts("SD CMD8 r1=");
    hal_uart_put_hex8(r1);
    hal_uart_puts(" r7=");
    hal_uart_put_hex8(r7[0]);
    hal_uart_put_hex8(r7[1]);
    hal_uart_put_hex8(r7[2]);
    hal_uart_put_hex8(r7[3]);
    hal_uart_puts("\r\n");
    if (r1 == 0x01 && r7[3] == 0xAA) {
        // SD v2
        uint16_t no_resp = 0;
        for (uint16_t i = 0; i < 8000; i++) {
            uint8_t r1_55 = sd_send_cmd(55, 0, 0x01, 0);
            hal_spi_sd_transfer(0xFF);
            r1 = sd_send_cmd(41, 0x40000000, 0x01, 0);
            sd_deselect();
            sd_last_acmd41_r1 = r1;
            if (r1 == 0x00) {
                break;
            }
            if (r1 == 0xFF) {
                no_resp++;
                sd_idle_clocks(2);
                if (no_resp == 50) {
                    hal_uart_puts("SD ACMD41 no response, re-issuing CMD0/CMD8\r\n");
                    sd_idle_clocks(10);
                    sd_send_cmd(0, 0, 0x95, 0);
                    sd_send_cmd(8, 0x000001AA, 0x87, r7);
                    no_resp = 0;
                }
            }
            hal_delay_ms(2);
            if (i == 0 || i == 4999) {
                hal_uart_puts("SD CMD55 r1=");
                hal_uart_put_hex8(r1_55);
                hal_uart_puts(" ACMD41 r1=");
                hal_uart_put_hex8(r1);
                hal_uart_puts("\r\n");
            }
        }
        hal_uart_puts("SD ACMD41 r1=");
        hal_uart_put_hex8(r1);
        hal_uart_puts("\r\n");
        if (r1 != 0x00) {
            goto out;
        }
        uint8_t ocr[4] = {0};
        r1 = sd_send_cmd(58, 0, 0x01, ocr);
        sd_last_cmd58_r1 = r1;
        for (uint8_t i = 0; i < 4; i++) {
            sd_last_cmd58_ocr[i] = ocr[i];
        }
        hal_uart_puts("SD CMD58 r1=");
        hal_uart_put_hex8(r1);
        hal_uart_puts(" ocr=");
        hal_uart_put_hex8(ocr[0]);
        hal_uart_put_hex8(ocr[1]);
        hal_uart_put_hex8(ocr[2]);
        hal_uart_put_hex8(ocr[3]);
        hal_uart_puts("\r\n");
        if (r1 == 0x00 && (ocr[0] & 0x40)) {
            sd_is_sdhc = 1;
        }
    } else {
        // SD v1 or MMC
        for (uint16_t i = 0; i < 8000; i++) {
            uint8_t r1_55 = sd_send_cmd(55, 0, 0x01, 0);
            hal_spi_sd_transfer(0xFF);
            r1 = sd_send_cmd(41, 0, 0x01, 0);
            sd_deselect();
            sd_last_acmd41_r1 = r1;
            if (r1 == 0x00) {
                break;
            }
            hal_delay_ms(2);
            if (i == 0 || i == 4999) {
                hal_uart_puts("SD CMD55 r1=");
                hal_uart_put_hex8(r1_55);
                hal_uart_puts(" ACMD41 r1=");
                hal_uart_put_hex8(r1);
                hal_uart_puts("\r\n");
            }
        }
        hal_uart_puts("SD ACMD41(v1) r1=");
        hal_uart_put_hex8(r1);
        hal_uart_puts("\r\n");
        if (r1 != 0x00) {
            // Try CMD1 (MMC) as fallback
            sd_deselect();
            hal_sd_cs_low();
            for (uint16_t i = 0; i < 5000; i++) {
                r1 = sd_send_cmd_noselect(1, 0, 0x01, 0);
                if (r1 == 0x00) {
                    break;
                }
                hal_delay_ms(2);
            }
            hal_sd_cs_high();
            hal_uart_puts("SD CMD1 r1=");
            hal_uart_put_hex8(r1);
            hal_uart_puts("\r\n");
        }
        if (r1 != 0x00) {
            goto out;
        }
    }

    ok = 1;

out:
    sd_deselect();
    hal_spi_sd_set_speed_fast();
    return ok;
}

static uint8_t sd_read_block(uint32_t lba, uint8_t *buf) {
    uint32_t addr = sd_is_sdhc ? lba : (lba << 9);
    for (uint8_t attempt = 0; attempt < 2; attempt++) {
        if (attempt == 0) {
            hal_spi_sd_set_speed_fast();
        } else {
            hal_spi_sd_set_speed_very_slow();
        }
        uint8_t r1 = sd_send_cmd(17, addr, 0x01, 0);
        if (r1 != 0x00) {
            sd_deselect();
            continue;
        }
        // Wait for data token
        uint16_t timeout = 50000;
        uint8_t token;
        do {
            token = hal_spi_sd_transfer(0xFF);
        } while (token == 0xFF && --timeout);
        if (token != 0xFE) {
            sd_deselect();
            continue;
        }
        hal_spi_sd_read_buffer(buf, 512);
        uint8_t crc[2];
        hal_spi_sd_read_buffer(crc, 2);
        sd_deselect();
        hal_spi_sd_set_speed_fast();
        return 1;
    }
    hal_spi_sd_set_speed_fast();
    return 0;
}

static uint8_t sd_is_fat_formatted(void) {
    uint8_t sector[512];
    if (!sd_read_block(0, sector)) {
        return 0;
    }
    if (sector[510] != 0x55 || sector[511] != 0xAA) {
        return 0;
    }

    // Check if sector 0 is a VBR with FAT signature.
    if ((sector[0] == 0xEB || sector[0] == 0xE9) &&
        ((sector[54] == 'F' && sector[55] == 'A' && sector[56] == 'T') ||
         (sector[82] == 'F' && sector[83] == 'A' && sector[84] == 'T'))) {
        return 1;
    }

    // MBR: check partition type for FAT and then read VBR.
    uint8_t ptype = sector[0x1BE + 4];
    if (ptype == 0x01 || ptype == 0x04 || ptype == 0x06 || ptype == 0x0B || ptype == 0x0C || ptype == 0x0E) {
        uint32_t lba = read_le32(&sector[0x1BE + 8]);
        if (!sd_read_block(lba, sector)) {
            return 0;
        }
        if (sector[510] != 0x55 || sector[511] != 0xAA) {
            return 0;
        }
        if ((sector[54] == 'F' && sector[55] == 'A' && sector[56] == 'T') ||
            (sector[82] == 'F' && sector[83] == 'A' && sector[84] == 'T')) {
            return 1;
        }
    }

    // GPT protective MBR
    if (ptype == 0xEE) {
        // GPT header at LBA 1
        if (!sd_read_block(1, sector)) {
            return 0;
        }
        if (!(sector[0] == 'E' && sector[1] == 'F' && sector[2] == 'I' &&
              sector[3] == ' ' && sector[4] == 'P' && sector[5] == 'A' &&
              sector[6] == 'R' && sector[7] == 'T')) {
            return 0;
        }
        uint32_t part_lba = read_le64_low32(&sector[72]); // partition entries LBA
        if (!sd_read_block(part_lba, sector)) {
            return 0;
        }
        uint32_t first_lba = read_le64_low32(&sector[32]); // first partition entry, first LBA
        if (!sd_read_block(first_lba, sector)) {
            return 0;
        }
        if (sector[510] != 0x55 || sector[511] != 0xAA) {
            return 0;
        }
        if ((sector[54] == 'F' && sector[55] == 'A' && sector[56] == 'T') ||
            (sector[82] == 'F' && sector[83] == 'A' && sector[84] == 'T')) {
            return 1;
        }
    }

    return 0;
}

static uint32_t fat32_cluster_to_lba(uint32_t cluster) {
    return fat32_first_data + (cluster - 2u) * fat32_sectors_per_cluster;
}

static uint8_t fat32_read_sector(uint32_t lba, uint8_t *buf) {
    return sd_read_block(lba, buf);
}

static uint8_t fat32_mount(void) {
    uint8_t sector[512];
    uint32_t lba = 0;

    if (!sd_read_block(0, sector)) {
        return 0;
    }

    if (sector[510] == 0x55 && sector[511] == 0xAA) {
        uint8_t ptype = sector[0x1BE + 4];
        if (ptype == 0x0B || ptype == 0x0C || ptype == 0x0E || ptype == 0x06 || ptype == 0x01 || ptype == 0x04) {
            lba = read_le32(&sector[0x1BE + 8]);
            if (!sd_read_block(lba, sector)) {
                return 0;
            }
        } else if (ptype == 0xEE) {
            if (!sd_read_block(1, sector)) {
                return 0;
            }
            if (!(sector[0] == 'E' && sector[1] == 'F' && sector[2] == 'I' &&
                  sector[3] == ' ' && sector[4] == 'P' && sector[5] == 'A' &&
                  sector[6] == 'R' && sector[7] == 'T')) {
                return 0;
            }
            uint32_t part_lba = read_le64_low32(&sector[72]);
            if (!sd_read_block(part_lba, sector)) {
                return 0;
            }
            lba = read_le64_low32(&sector[32]);
            if (!sd_read_block(lba, sector)) {
                return 0;
            }
        }
    }

    if (sector[510] != 0x55 || sector[511] != 0xAA) {
        return 0;
    }

    uint16_t bytes_per_sector = (uint16_t)sector[11] | ((uint16_t)sector[12] << 8);
    if (bytes_per_sector != 512) {
        return 0;
    }
    fat32_sectors_per_cluster = sector[13];
    uint16_t reserved = (uint16_t)sector[14] | ((uint16_t)sector[15] << 8);
    uint8_t num_fats = sector[16];
    fat32_fat_size = (uint32_t)sector[36] |
                     ((uint32_t)sector[37] << 8) |
                     ((uint32_t)sector[38] << 16) |
                     ((uint32_t)sector[39] << 24);
    fat32_root_cluster = (uint32_t)sector[44] |
                         ((uint32_t)sector[45] << 8) |
                         ((uint32_t)sector[46] << 16) |
                         ((uint32_t)sector[47] << 24);

    if (fat32_sectors_per_cluster == 0 || fat32_fat_size == 0 || fat32_root_cluster < 2) {
        return 0;
    }

    fat32_lba_start = lba;
    fat32_first_fat = fat32_lba_start + reserved;
    fat32_first_data = fat32_lba_start + reserved + (uint32_t)num_fats * fat32_fat_size;
    sd_fat32_ready = 1;
    return 1;
}

static uint32_t fat32_read_fat(uint32_t cluster, uint8_t *sector) {
    uint32_t fat_offset = cluster * 4u;
    uint32_t fat_sector = fat32_first_fat + (fat_offset / 512u);
    uint16_t fat_index = (uint16_t)(fat_offset % 512u);
    if (!fat32_read_sector(fat_sector, sector)) {
        return 0x0FFFFFFF;
    }
    uint32_t entry = (uint32_t)sector[fat_index] |
                     ((uint32_t)sector[fat_index + 1] << 8) |
                     ((uint32_t)sector[fat_index + 2] << 16) |
                     ((uint32_t)sector[fat_index + 3] << 24);
    return entry & 0x0FFFFFFF;
}

static void fat32_name_to_83(const char *name, char out[11]) {
    for (uint8_t i = 0; i < 11; i++) {
        out[i] = ' ';
    }
    uint8_t i = 0;
    while (*name && *name != '.' && i < 8) {
        char c = *name++;
        if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
        out[i++] = c;
    }
    if (*name == '.') {
        name++;
        i = 8;
        while (*name && i < 11) {
            char c = *name++;
            if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
            out[i++] = c;
        }
    }
}

static uint8_t fat32_find_file_root(const char *name, uint32_t *first_cluster, uint32_t *file_size) {
    if (!sd_fat32_ready) {
        return 0;
    }
    char name83[11];
    fat32_name_to_83(name, name83);

    uint8_t sector[512];
    uint32_t cluster = fat32_root_cluster;
    while (cluster < 0x0FFFFFF8) {
        uint32_t lba = fat32_cluster_to_lba(cluster);
        for (uint8_t s = 0; s < fat32_sectors_per_cluster; s++) {
            if (!fat32_read_sector(lba + s, sector)) {
                return 0;
            }
            for (uint16_t off = 0; off < 512; off += 32) {
                uint8_t first = sector[off];
                if (first == 0x00) {
                    return 0;
                }
                if (first == 0xE5) {
                    continue;
                }
                uint8_t attr = sector[off + 11];
                if (attr == 0x0F) {
                    continue;
                }
                uint8_t match = 1;
                for (uint8_t i = 0; i < 11; i++) {
                    if ((char)sector[off + i] != name83[i]) {
                        match = 0;
                        break;
                    }
                }
                if (match) {
                    uint16_t hi = (uint16_t)sector[off + 20] | ((uint16_t)sector[off + 21] << 8);
                    uint16_t lo = (uint16_t)sector[off + 26] | ((uint16_t)sector[off + 27] << 8);
                    *first_cluster = ((uint32_t)hi << 16) | lo;
                    *file_size = (uint32_t)sector[off + 28] |
                                 ((uint32_t)sector[off + 29] << 8) |
                                 ((uint32_t)sector[off + 30] << 16) |
                                 ((uint32_t)sector[off + 31] << 24);
                    return 1;
                }
            }
        }
        cluster = fat32_read_fat(cluster, sector);
    }
    return 0;
}

typedef void (*fat32_sink_fn)(const uint8_t *data, uint16_t len, void *ctx);

static uint8_t fat32_read_file_stream_ctx(const char *name, uint32_t expected_size,
                                          fat32_sink_fn sink, void *ctx) {
    uint32_t first_cluster = 0;
    uint32_t file_size = 0;
    if (!fat32_find_file_root(name, &first_cluster, &file_size)) {
        hal_uart_puts("FAT: file not found: ");
        hal_uart_puts(name);
        hal_uart_puts("\r\n");
        return 0;
    }
    if (expected_size && file_size != expected_size) {
        hal_uart_puts("FAT: size mismatch: ");
        hal_uart_puts(name);
        hal_uart_puts(" got=");
        hal_uart_put_hex8((uint8_t)(file_size >> 24));
        hal_uart_put_hex8((uint8_t)(file_size >> 16));
        hal_uart_put_hex8((uint8_t)(file_size >> 8));
        hal_uart_put_hex8((uint8_t)(file_size));
        hal_uart_puts("\r\n");
        return 0;
    }

    uint8_t sector[512];
    uint32_t cluster = first_cluster;
    uint32_t remaining = file_size;
    while (cluster < 0x0FFFFFF8 && remaining) {
        uint32_t lba = fat32_cluster_to_lba(cluster);
        for (uint8_t s = 0; s < fat32_sectors_per_cluster && remaining; s++) {
            if (!fat32_read_sector(lba + s, sector)) {
                hal_uart_puts("FAT: read error\r\n");
                return 0;
            }
            uint16_t chunk = remaining > 512 ? 512 : (uint16_t)remaining;
            sink(sector, chunk, ctx);
            remaining -= chunk;
        }
        cluster = fat32_read_fat(cluster, sector);
    }
    return remaining == 0;
}

static void fat32_sink_shim(const uint8_t *data, uint16_t len, void *ctx) {
    void (*sink)(const uint8_t *data, uint16_t len) = (void (*)(const uint8_t *, uint16_t))ctx;
    sink(data, len);
}

static uint8_t fat32_read_file_stream(const char *name, uint32_t expected_size,
                                      void (*sink)(const uint8_t *data, uint16_t len)) {
    return fat32_read_file_stream_ctx(name, expected_size, fat32_sink_shim, (void *)sink);
}

static void tft_stream_bytes_sd(const uint8_t *data, uint16_t len) {
    hal_sd_cs_high();
    display_stream_bytes(data, len);
}

typedef struct {
    uint8_t *dst;
    uint32_t offset;
    uint32_t size;
    uint8_t stream;
} cache_sink_t;

static void tft_stream_bytes_cache(const uint8_t *data, uint16_t len, void *ctx) {
    cache_sink_t *sink = (cache_sink_t *)ctx;
    if (sink->dst && sink->offset + len <= sink->size) {
        memcpy(sink->dst + sink->offset, data, len);
    }
    sink->offset += len;
    if (sink->stream) {
        hal_sd_cs_high();
        display_stream_bytes(data, len);
    }
}

static void fat32_print_name83(const uint8_t *entry) {
    char name[13];
    uint8_t n = 0;
    for (uint8_t i = 0; i < 8; i++) {
        if (entry[i] == ' ') break;
        name[n++] = (char)entry[i];
    }
    if (entry[8] != ' ') {
        name[n++] = '.';
        for (uint8_t i = 8; i < 11; i++) {
            if (entry[i] == ' ') break;
            name[n++] = (char)entry[i];
        }
    }
    name[n] = '\0';
    hal_uart_puts(name);
}

static void fat32_list_root(void) {
    if (!sd_fat32_ready) {
        hal_uart_puts("FAT: not mounted\r\n");
        return;
    }
    uint8_t sector[512];
    uint32_t cluster = fat32_root_cluster;
    hal_uart_puts("\r\nRoot dir:\r\n");
    while (cluster < 0x0FFFFFF8) {
        uint32_t lba = fat32_cluster_to_lba(cluster);
        for (uint8_t s = 0; s < fat32_sectors_per_cluster; s++) {
            if (!fat32_read_sector(lba + s, sector)) {
                hal_uart_puts("FAT: read error\r\n");
                return;
            }
            for (uint16_t off = 0; off < 512; off += 32) {
                uint8_t first = sector[off];
                if (first == 0x00) {
                    return;
                }
                if (first == 0xE5) {
                    continue;
                }
                uint8_t attr = sector[off + 11];
                if (attr == 0x0F) {
                    continue; // skip LFN
                }
                if (attr & 0x08) {
                    continue; // volume label
                }
                fat32_print_name83(&sector[off]);
                hal_uart_puts("  size=");
                uint32_t size = (uint32_t)sector[off + 28] |
                                ((uint32_t)sector[off + 29] << 8) |
                                ((uint32_t)sector[off + 30] << 16) |
                                ((uint32_t)sector[off + 31] << 24);
                hal_uart_put_hex8((uint8_t)(size >> 24));
                hal_uart_put_hex8((uint8_t)(size >> 16));
                hal_uart_put_hex8((uint8_t)(size >> 8));
                hal_uart_put_hex8((uint8_t)(size));
                hal_uart_puts("\r\n");
            }
        }
        cluster = fat32_read_fat(cluster, sector);
    }
}

static void sd_probe(void) {
    hal_uart_puts("\r\nSD probe:\r\n");
    hal_uart_puts(" MISO pin state: ");
    hal_uart_put_hex8(hal_miso_state());
    hal_uart_puts("\r\n CS high, xfer=0x");
    hal_sd_cs_high();
    hal_uart_put_hex8(hal_spi_sd_transfer(0xFF));
    hal_uart_puts("\r\n CS low,  xfer=0x");
    hal_sd_cs_low();
    hal_uart_put_hex8(hal_spi_sd_transfer(0xFF));
    hal_sd_cs_high();
    hal_uart_puts("\r\n");
}

void card_reader_init(card_reader_status_t *status) {
    hal_uart_init();
    hal_uart_puts("\r\nBOOT\r\n");
    status->sd_ok = sd_init();
    sd_last_fat_format_ok = status->sd_ok ? sd_is_fat_formatted() : 0;
    sd_last_fat_mount_ok = 0;
    if (status->sd_ok && sd_last_fat_format_ok) {
        sd_last_fat_mount_ok = fat32_mount();
    }
    status->fat_ok = sd_last_fat_mount_ok;

}

void card_reader_print_status(const card_reader_status_t *status) {
    hal_uart_puts("\r\nStatus:\r\n");
    hal_uart_puts(" Display: OK\r\n");
    hal_uart_puts(" SD init: ");
    hal_uart_puts(status->sd_ok ? "OK" : "FAIL");
    hal_uart_puts("\r\n");
    hal_uart_puts(" SD type: ");
    hal_uart_puts(sd_is_sdhc ? "SDHC/SDXC" : "SDSC");
    hal_uart_puts("\r\n");
    hal_uart_puts(" FAT: ");
    hal_uart_puts(status->fat_ok ? "YES" : "NO");
    hal_uart_puts("\r\n");
    hal_uart_puts(" Commands: s=status, r=read LBA0, d=sd probe, l=list files, p=play SD images\r\n");
}

uint8_t card_reader_draw_raw565(const char *name, uint16_t width, uint16_t height) {
    uint32_t expected = (uint32_t)width * (uint32_t)height * 2u;
    display_set_addr_window(width, height);
#if IMAGE_CACHE_ENABLE
    image_cache_entry_t *hit = cache_find(name);
    if (hit && hit->data && hit->size == expected) {
        hal_sd_cs_high();
        if (expected <= 65535u) {
            display_stream_bytes(hit->data, (uint16_t)expected);
        } else {
            uint32_t remaining = expected;
            uint32_t offset = 0;
            while (remaining) {
                uint16_t chunk = remaining > 4096 ? 4096 : (uint16_t)remaining;
                display_stream_bytes(hit->data + offset, chunk);
                remaining -= chunk;
                offset += chunk;
            }
        }
        return 1;
    }

    image_cache_entry_t *slot = cache_reserve(name, expected);
    if (slot && slot->data) {
        cache_sink_t sink = {
            .dst = slot->data,
            .offset = 0,
            .size = slot->size,
            .stream = 1,
        };
        uint8_t ok = fat32_read_file_stream_ctx(name, expected, tft_stream_bytes_cache, &sink);
        if (!ok) {
            free(slot->data);
            slot->data = NULL;
            slot->valid = 0;
        }
        return ok;
    }
#endif
    return fat32_read_file_stream(name, expected, tft_stream_bytes_sd);
}

uint8_t card_reader_sd_is_sdhc(void) {
    return sd_is_sdhc;
}

uint8_t card_reader_fat_ready(void) {
    return sd_fat32_ready;
}

uint32_t card_reader_fat_lba_start(void) {
    return fat32_lba_start;
}

uint32_t card_reader_fat_first_fat(void) {
    return fat32_first_fat;
}

uint32_t card_reader_fat_first_data(void) {
    return fat32_first_data;
}

uint32_t card_reader_fat_root_cluster(void) {
    return fat32_root_cluster;
}

uint8_t card_reader_fat_sectors_per_cluster(void) {
    return fat32_sectors_per_cluster;
}

uint32_t card_reader_fat_size(void) {
    return fat32_fat_size;
}

uint8_t card_reader_sd_cmd0_r1(void) {
    return sd_last_cmd0_r1;
}

uint8_t card_reader_sd_cmd8_r1(void) {
    return sd_last_cmd8_r1;
}

uint8_t card_reader_sd_acmd41_r1(void) {
    return sd_last_acmd41_r1;
}

uint8_t card_reader_sd_cmd58_r1(void) {
    return sd_last_cmd58_r1;
}

const uint8_t *card_reader_sd_cmd8_r7(void) {
    return sd_last_cmd8_r7;
}

const uint8_t *card_reader_sd_cmd58_ocr(void) {
    return sd_last_cmd58_ocr;
}

uint8_t card_reader_sd_fat_format_ok(void) {
    return sd_last_fat_format_ok;
}

uint8_t card_reader_sd_fat_mount_ok(void) {
    return sd_last_fat_mount_ok;
}


void card_reader_handle_cli(const card_reader_status_t *status,
                            const char *image1,
                            const char *image2,
                            uint16_t width,
                            uint16_t height) {
    char c;
    if (!hal_uart_getc_nonblock(&c)) {
        return;
    }
    if (c == 's' || c == 'S') {
        card_reader_print_status(status);
    } else if (c == 'r' || c == 'R') {
        uint8_t sector[512];
        hal_uart_puts("\r\nRead LBA0: ");
        if (status->sd_ok && sd_read_block(0, sector)) {
            hal_uart_puts("OK ");
            hal_uart_puts("sig=");
            hal_uart_put_hex8(sector[510]);
            hal_uart_put_hex8(sector[511]);
            hal_uart_puts(" head=");
            for (uint8_t i = 0; i < 16; i++) {
                hal_uart_put_hex8(sector[i]);
            }
            hal_uart_puts("\r\n");
        } else {
            hal_uart_puts("FAIL\r\n");
        }
    } else if (c == 'd' || c == 'D') {
        sd_probe();
    } else if (c == 'l' || c == 'L') {
        fat32_list_root();
    } else if (c == 'p' || c == 'P') {
        hal_uart_puts("\r\nPlay SD images...\r\n");
        if (status->sd_ok && status->fat_ok) {
            if (!card_reader_draw_raw565(image1, width, height)) {
                hal_uart_puts("Failed: ");
                hal_uart_puts(image1);
                hal_uart_puts("\r\n");
            }
            if (!card_reader_draw_raw565(image2, width, height)) {
                hal_uart_puts("Failed: ");
                hal_uart_puts(image2);
                hal_uart_puts("\r\n");
            }
        } else {
            hal_uart_puts("SD/FAT not ready\r\n");
        }
    }
}
