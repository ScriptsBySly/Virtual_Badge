#include "card_reader.h"

#ifndef BAUD
#define BAUD 9600UL
#endif

#include <avr/io.h>
#include <util/delay.h>
#include <util/setbaud.h>

#include "display.h"
// SD card (SPI) on Nano: CS = D4 (PD4), SCK = D13 (PB5), MOSI = D11 (PB3), MISO = D12 (PB4)
#define SD_CS_PORT  PORTD
#define SD_CS_DDR   DDRD
#define SD_CS_PIN   PD4
#define SD_CS_LOW()  (SD_CS_PORT &= ~(1 << SD_CS_PIN))
#define SD_CS_HIGH() (SD_CS_PORT |= (1 << SD_CS_PIN))

static uint8_t sd_is_sdhc = 0;
static uint8_t sd_fat32_ready = 0;
static uint32_t fat32_lba_start = 0;
static uint32_t fat32_first_fat = 0;
static uint32_t fat32_first_data = 0;
static uint32_t fat32_fat_size = 0;
static uint8_t fat32_sectors_per_cluster = 0;
static uint32_t fat32_root_cluster = 0;

void spi_init(void) {
    // MOSI, SCK, SS as output. MISO input.
    DDRB |= (1 << PB3) | (1 << PB5) | (1 << PB2);
    DDRB &= ~(1 << PB4);
    PORTB |= (1 << PB4); // pull-up on MISO
    PORTB |= (1 << PB2); // keep SS high (stay master)
    // Enable SPI, Master, mode 0.
    SPCR = (1 << SPE) | (1 << MSTR);
    SPSR = 0;
}

void spi_set_speed_very_slow(void) {
    // fosc/256
    SPCR |= (1 << SPR1) | (1 << SPR0);
    SPSR &= ~(1 << SPI2X);
}

void spi_set_speed_fast(void) {
    // fosc/2
    SPCR &= ~((1 << SPR1) | (1 << SPR0));
    SPSR |= (1 << SPI2X);
}

uint8_t spi_transfer(uint8_t data) {
    SPDR = data;
    while (!(SPSR & (1 << SPIF))) {
        // wait
    }
    return SPDR;
}

void spi_write(uint8_t data) {
    (void)spi_transfer(data);
}

static void uart_init(void) {
    UBRR0H = UBRRH_VALUE;
    UBRR0L = UBRRL_VALUE;
#if USE_2X
    UCSR0A = (1 << U2X0);
#else
    UCSR0A = 0;
#endif
    UCSR0B = (1 << TXEN0) | (1 << RXEN0);
    UCSR0C = (1 << UCSZ01) | (1 << UCSZ00);
}

static void uart_putc(char c) {
    while (!(UCSR0A & (1 << UDRE0))) {
    }
    UDR0 = (uint8_t)c;
}

static void uart_puts(const char *s) {
    while (*s) {
        uart_putc(*s++);
    }
}

static void uart_put_hex8(uint8_t v) {
    const char hex[] = "0123456789ABCDEF";
    uart_putc(hex[(v >> 4) & 0xF]);
    uart_putc(hex[v & 0xF]);
}

static uint8_t uart_getc_nonblock(char *out) {
    if (UCSR0A & (1 << RXC0)) {
        *out = (char)UDR0;
        return 1;
    }
    return 0;
}

static void sd_deselect(void) {
    SD_CS_HIGH();
    spi_transfer(0xFF);
}

static void sd_idle_clocks(uint8_t count) {
    SD_CS_HIGH();
    for (uint8_t i = 0; i < count; i++) {
        spi_transfer(0xFF);
    }
}

static uint8_t sd_select(void) {
    SD_CS_LOW();
    spi_transfer(0xFF);
    return 1;
}

static uint8_t sd_wait_ready(uint16_t timeout_ms) {
    while (timeout_ms--) {
        if (spi_transfer(0xFF) == 0xFF) {
            return 1;
        }
        _delay_ms(1);
    }
    return 0;
}

static uint8_t sd_send_cmd(uint8_t cmd, uint32_t arg, uint8_t crc, uint8_t *r7) {
    sd_deselect();
    sd_select();
    if (!sd_wait_ready(250)) {
        return 0xFF;
    }

    spi_transfer(0x40 | cmd);
    spi_transfer((uint8_t)(arg >> 24));
    spi_transfer((uint8_t)(arg >> 16));
    spi_transfer((uint8_t)(arg >> 8));
    spi_transfer((uint8_t)(arg));
    spi_transfer(crc);

    uint8_t r1 = 0xFF;
    for (uint8_t i = 0; i < 100; i++) {
        r1 = spi_transfer(0xFF);
        if ((r1 & 0x80) == 0) {
            break;
        }
    }

    if (r7) {
        for (uint8_t i = 0; i < 4; i++) {
            r7[i] = spi_transfer(0xFF);
        }
    }
    return r1;
}

static uint8_t sd_send_cmd_noselect(uint8_t cmd, uint32_t arg, uint8_t crc, uint8_t *r7) {
    if (!sd_wait_ready(250)) {
        return 0xFF;
    }

    spi_transfer(0x40 | cmd);
    spi_transfer((uint8_t)(arg >> 24));
    spi_transfer((uint8_t)(arg >> 16));
    spi_transfer((uint8_t)(arg >> 8));
    spi_transfer((uint8_t)(arg));
    spi_transfer(crc);

    uint8_t r1 = 0xFF;
    for (uint8_t i = 0; i < 100; i++) {
        r1 = spi_transfer(0xFF);
        if ((r1 & 0x80) == 0) {
            break;
        }
    }

    if (r7) {
        for (uint8_t i = 0; i < 4; i++) {
            r7[i] = spi_transfer(0xFF);
        }
    }
    return r1;
}

static uint8_t sd_init(void) {
    SD_CS_DDR |= (1 << SD_CS_PIN);
    SD_CS_HIGH();

    spi_set_speed_very_slow();
    uint8_t ok = 0;

    _delay_ms(500);

    // Send 160 clocks with CS high.
    sd_idle_clocks(20);

    uint8_t r7[4] = {0};
    uint8_t r1 = 0xFF;
    for (uint8_t attempt = 0; attempt < 10; attempt++) {
        r1 = sd_send_cmd(0, 0, 0x95, 0);
        if (r1 == 0x01) {
            break;
        }
        _delay_ms(10);
    }
    uart_puts("SD CMD0 r1=");
    uart_put_hex8(r1);
    uart_puts("\r\n");
    if (r1 != 0x01) {
        goto out;
    }

    r1 = sd_send_cmd(8, 0x000001AA, 0x87, r7);
    uart_puts("SD CMD8 r1=");
    uart_put_hex8(r1);
    uart_puts(" r7=");
    uart_put_hex8(r7[0]);
    uart_put_hex8(r7[1]);
    uart_put_hex8(r7[2]);
    uart_put_hex8(r7[3]);
    uart_puts("\r\n");
    if (r1 == 0x01 && r7[3] == 0xAA) {
        // SD v2
        uint16_t no_resp = 0;
        for (uint16_t i = 0; i < 8000; i++) {
            uint8_t r1_55 = sd_send_cmd(55, 0, 0x01, 0);
            spi_transfer(0xFF);
            r1 = sd_send_cmd(41, 0x40000000, 0x01, 0);
            sd_deselect();
            if (r1 == 0x00) {
                break;
            }
            if (r1 == 0xFF) {
                no_resp++;
                sd_idle_clocks(2);
                if (no_resp == 50) {
                    uart_puts("SD ACMD41 no response, re-issuing CMD0/CMD8\r\n");
                    sd_idle_clocks(10);
                    sd_send_cmd(0, 0, 0x95, 0);
                    sd_send_cmd(8, 0x000001AA, 0x87, r7);
                    no_resp = 0;
                }
            }
            _delay_ms(2);
            if (i == 0 || i == 4999) {
                uart_puts("SD CMD55 r1=");
                uart_put_hex8(r1_55);
                uart_puts(" ACMD41 r1=");
                uart_put_hex8(r1);
                uart_puts("\r\n");
            }
        }
        uart_puts("SD ACMD41 r1=");
        uart_put_hex8(r1);
        uart_puts("\r\n");
        if (r1 != 0x00) {
            goto out;
        }
        uint8_t ocr[4] = {0};
        r1 = sd_send_cmd(58, 0, 0x01, ocr);
        uart_puts("SD CMD58 r1=");
        uart_put_hex8(r1);
        uart_puts(" ocr=");
        uart_put_hex8(ocr[0]);
        uart_put_hex8(ocr[1]);
        uart_put_hex8(ocr[2]);
        uart_put_hex8(ocr[3]);
        uart_puts("\r\n");
        if (r1 == 0x00 && (ocr[0] & 0x40)) {
            sd_is_sdhc = 1;
        }
    } else {
        // SD v1 or MMC
        for (uint16_t i = 0; i < 8000; i++) {
            uint8_t r1_55 = sd_send_cmd(55, 0, 0x01, 0);
            spi_transfer(0xFF);
            r1 = sd_send_cmd(41, 0, 0x01, 0);
            sd_deselect();
            if (r1 == 0x00) {
                break;
            }
            _delay_ms(2);
            if (i == 0 || i == 4999) {
                uart_puts("SD CMD55 r1=");
                uart_put_hex8(r1_55);
                uart_puts(" ACMD41 r1=");
                uart_put_hex8(r1);
                uart_puts("\r\n");
            }
        }
        uart_puts("SD ACMD41(v1) r1=");
        uart_put_hex8(r1);
        uart_puts("\r\n");
        if (r1 != 0x00) {
            // Try CMD1 (MMC) as fallback
            sd_deselect();
            SD_CS_LOW();
            for (uint16_t i = 0; i < 5000; i++) {
                r1 = sd_send_cmd_noselect(1, 0, 0x01, 0);
                if (r1 == 0x00) {
                    break;
                }
                _delay_ms(2);
            }
            SD_CS_HIGH();
            uart_puts("SD CMD1 r1=");
            uart_put_hex8(r1);
            uart_puts("\r\n");
        }
        if (r1 != 0x00) {
            goto out;
        }
    }

    ok = 1;

out:
    sd_deselect();
    spi_set_speed_fast();
    return ok;
}

static uint8_t sd_read_block(uint32_t lba, uint8_t *buf) {
    uint32_t addr = sd_is_sdhc ? lba : (lba << 9);
    for (uint8_t attempt = 0; attempt < 2; attempt++) {
        if (attempt == 0) {
            spi_set_speed_fast();
        } else {
            spi_set_speed_very_slow();
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
            token = spi_transfer(0xFF);
        } while (token == 0xFF && --timeout);
        if (token != 0xFE) {
            sd_deselect();
            continue;
        }
        for (uint16_t i = 0; i < 512; i++) {
            buf[i] = spi_transfer(0xFF);
        }
        spi_transfer(0xFF); // CRC
        spi_transfer(0xFF);
        sd_deselect();
        spi_set_speed_fast();
        return 1;
    }
    spi_set_speed_fast();
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
        uint32_t lba = (uint32_t)sector[0x1BE + 8] |
                       ((uint32_t)sector[0x1BE + 9] << 8) |
                       ((uint32_t)sector[0x1BE + 10] << 16) |
                       ((uint32_t)sector[0x1BE + 11] << 24);
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

    if (sector[510] == 0x55 && sector[511] == 0xAA &&
        (sector[0x1BE + 4] == 0x0B || sector[0x1BE + 4] == 0x0C)) {
        lba = (uint32_t)sector[0x1BE + 8] |
              ((uint32_t)sector[0x1BE + 9] << 8) |
              ((uint32_t)sector[0x1BE + 10] << 16) |
              ((uint32_t)sector[0x1BE + 11] << 24);
        if (!sd_read_block(lba, sector)) {
            return 0;
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

static uint8_t fat32_read_file_stream(const char *name, uint32_t expected_size,
                                      void (*sink)(const uint8_t *data, uint16_t len)) {
    uint32_t first_cluster = 0;
    uint32_t file_size = 0;
    if (!fat32_find_file_root(name, &first_cluster, &file_size)) {
        uart_puts("FAT: file not found: ");
        uart_puts(name);
        uart_puts("\r\n");
        return 0;
    }
    if (expected_size && file_size != expected_size) {
        uart_puts("FAT: size mismatch: ");
        uart_puts(name);
        uart_puts(" got=");
        uart_put_hex8((uint8_t)(file_size >> 24));
        uart_put_hex8((uint8_t)(file_size >> 16));
        uart_put_hex8((uint8_t)(file_size >> 8));
        uart_put_hex8((uint8_t)(file_size));
        uart_puts("\r\n");
        return 0;
    }

    uint8_t sector[512];
    uint32_t cluster = first_cluster;
    uint32_t remaining = file_size;
    while (cluster < 0x0FFFFFF8 && remaining) {
        uint32_t lba = fat32_cluster_to_lba(cluster);
        for (uint8_t s = 0; s < fat32_sectors_per_cluster && remaining; s++) {
            if (!fat32_read_sector(lba + s, sector)) {
                uart_puts("FAT: read error\r\n");
                return 0;
            }
            uint16_t chunk = remaining > 512 ? 512 : (uint16_t)remaining;
            sink(sector, chunk);
            remaining -= chunk;
        }
        cluster = fat32_read_fat(cluster, sector);
    }
    return remaining == 0;
}

static void tft_stream_bytes_sd(const uint8_t *data, uint16_t len) {
    SD_CS_HIGH();
    display_stream_bytes(data, len);
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
    uart_puts(name);
}

static void fat32_list_root(void) {
    if (!sd_fat32_ready) {
        uart_puts("FAT: not mounted\r\n");
        return;
    }
    uint8_t sector[512];
    uint32_t cluster = fat32_root_cluster;
    uart_puts("\r\nRoot dir:\r\n");
    while (cluster < 0x0FFFFFF8) {
        uint32_t lba = fat32_cluster_to_lba(cluster);
        for (uint8_t s = 0; s < fat32_sectors_per_cluster; s++) {
            if (!fat32_read_sector(lba + s, sector)) {
                uart_puts("FAT: read error\r\n");
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
                uart_puts("  size=");
                uint32_t size = (uint32_t)sector[off + 28] |
                                ((uint32_t)sector[off + 29] << 8) |
                                ((uint32_t)sector[off + 30] << 16) |
                                ((uint32_t)sector[off + 31] << 24);
                uart_put_hex8((uint8_t)(size >> 24));
                uart_put_hex8((uint8_t)(size >> 16));
                uart_put_hex8((uint8_t)(size >> 8));
                uart_put_hex8((uint8_t)(size));
                uart_puts("\r\n");
            }
        }
        cluster = fat32_read_fat(cluster, sector);
    }
}

static void sd_probe(void) {
    uart_puts("\r\nSD probe:\r\n");
    uart_puts(" MISO pin state: ");
    uart_put_hex8((PINB & (1 << PB4)) ? 1 : 0);
    uart_puts("\r\n CS high, xfer=0x");
    SD_CS_HIGH();
    uart_put_hex8(spi_transfer(0xFF));
    uart_puts("\r\n CS low,  xfer=0x");
    SD_CS_LOW();
    uart_put_hex8(spi_transfer(0xFF));
    SD_CS_HIGH();
    uart_puts("\r\n");
}

void card_reader_init(card_reader_status_t *status) {
    uart_init();
    uart_puts("\r\nBOOT\r\n");
    status->sd_ok = sd_init();
    status->fat_ok = status->sd_ok ? sd_is_fat_formatted() : 0;
    if (status->sd_ok && status->fat_ok) {
        status->fat_ok = fat32_mount();
    }
}

void card_reader_print_status(const card_reader_status_t *status) {
    uart_puts("\r\nStatus:\r\n");
    uart_puts(" Display: OK\r\n");
    uart_puts(" SD init: ");
    uart_puts(status->sd_ok ? "OK" : "FAIL");
    uart_puts("\r\n");
    uart_puts(" SD type: ");
    uart_puts(sd_is_sdhc ? "SDHC/SDXC" : "SDSC");
    uart_puts("\r\n");
    uart_puts(" FAT: ");
    uart_puts(status->fat_ok ? "YES" : "NO");
    uart_puts("\r\n");
    uart_puts(" Commands: s=status, r=read LBA0, d=sd probe, l=list files, p=play SD images\r\n");
}

uint8_t card_reader_draw_raw565(const char *name, uint16_t width, uint16_t height) {
    uint32_t expected = (uint32_t)width * (uint32_t)height * 2u;
    display_set_addr_window(width, height);
    return fat32_read_file_stream(name, expected, tft_stream_bytes_sd);
}

void card_reader_handle_cli(const card_reader_status_t *status,
                            const char *image1,
                            const char *image2,
                            uint16_t width,
                            uint16_t height) {
    char c;
    if (!uart_getc_nonblock(&c)) {
        return;
    }
    if (c == 's' || c == 'S') {
        card_reader_print_status(status);
    } else if (c == 'r' || c == 'R') {
        uint8_t sector[512];
        uart_puts("\r\nRead LBA0: ");
        if (status->sd_ok && sd_read_block(0, sector)) {
            uart_puts("OK ");
            uart_puts("sig=");
            uart_put_hex8(sector[510]);
            uart_put_hex8(sector[511]);
            uart_puts(" head=");
            for (uint8_t i = 0; i < 16; i++) {
                uart_put_hex8(sector[i]);
            }
            uart_puts("\r\n");
        } else {
            uart_puts("FAIL\r\n");
        }
    } else if (c == 'd' || c == 'D') {
        sd_probe();
    } else if (c == 'l' || c == 'L') {
        fat32_list_root();
    } else if (c == 'p' || c == 'P') {
        uart_puts("\r\nPlay SD images...\r\n");
        if (status->sd_ok && status->fat_ok) {
            if (!card_reader_draw_raw565(image1, width, height)) {
                uart_puts("Failed: ");
                uart_puts(image1);
                uart_puts("\r\n");
            }
            if (!card_reader_draw_raw565(image2, width, height)) {
                uart_puts("Failed: ");
                uart_puts(image2);
                uart_puts("\r\n");
            }
        } else {
            uart_puts("SD/FAT not ready\r\n");
        }
    }
}
