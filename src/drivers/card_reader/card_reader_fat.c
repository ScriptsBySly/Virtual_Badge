#include "drivers/card_reader/card_reader_fat.h"

#include "drivers/card_reader/card_reader_spi.h"
#include "hal/hal.h"

#include <string.h>

enum {
    CARD_READER_SD_READ_ATTEMPTS = 2,
    CARD_READER_FAT_SECTOR_SIZE = 512,
    CARD_READER_FAT_DIR_ENTRY_SIZE = 32,
    CARD_READER_FAT_NAME83_LEN = 11,
};

#define CARD_READER_MBR_SIG_OFFSET 510
#define CARD_READER_MBR_SIG_BYTE0 0x55
#define CARD_READER_MBR_SIG_BYTE1 0xAA

#define CARD_READER_VBR_JMP0 0xEB
#define CARD_READER_VBR_JMP1 0xE9

#define CARD_READER_FAT_SIG_OFFSET_FAT12_16 54
#define CARD_READER_FAT_SIG_OFFSET_FAT32 82
#define CARD_READER_FAT_SIG_LEN 3

#define CARD_READER_MBR_PART_ENTRY0 0x1BE
#define CARD_READER_MBR_PART_TYPE_OFFSET 4
#define CARD_READER_MBR_PART_LBA_OFFSET 8

#define CARD_READER_PTYPE_FAT12 0x01
#define CARD_READER_PTYPE_FAT16_LT32M 0x04
#define CARD_READER_PTYPE_FAT16 0x06
#define CARD_READER_PTYPE_FAT32_CHS 0x0B
#define CARD_READER_PTYPE_FAT32_LBA 0x0C
#define CARD_READER_PTYPE_FAT16_LBA 0x0E
#define CARD_READER_PTYPE_GPT_PROTECTIVE 0xEE

#define CARD_READER_GPT_HEADER_LBA 1
#define CARD_READER_GPT_SIGNATURE_LEN 8

#define CARD_READER_GPT_PART_ENTRIES_LBA_OFFSET 72
#define CARD_READER_GPT_FIRST_PART_LBA_OFFSET 32

#define CARD_READER_BPB_BYTES_PER_SECTOR_OFFSET 11
#define CARD_READER_BPB_SECTORS_PER_CLUSTER_OFFSET 13
#define CARD_READER_BPB_RESERVED_SECTORS_OFFSET 14
#define CARD_READER_BPB_NUM_FATS_OFFSET 16
#define CARD_READER_BPB_FAT_SIZE_OFFSET 36
#define CARD_READER_BPB_ROOT_CLUSTER_OFFSET 44

#define CARD_READER_SD_DATA_TOKEN 0xFE
#define CARD_READER_SD_READ_TIMEOUT 50000
#define CARD_READER_SD_R1_READY 0x00
#define CARD_READER_SD_IDLE_BYTE 0xFF

#define CARD_READER_FAT_EOC_MARKER 0x0FFFFFF8
#define CARD_READER_FAT_ENTRY_MASK 0x0FFFFFFF
#define CARD_READER_FAT_DIR_ENTRY_FREE 0x00
#define CARD_READER_FAT_DIR_ENTRY_DELETED 0xE5
#define CARD_READER_FAT_DIR_ATTR_OFFSET 11
#define CARD_READER_FAT_DIR_ATTR_LFN 0x0F
#define CARD_READER_FAT_DIR_CLUSTER_HI_OFFSET 20
#define CARD_READER_FAT_DIR_CLUSTER_LO_OFFSET 26
#define CARD_READER_FAT_DIR_FILE_SIZE_OFFSET 28

static const char k_card_reader_fat_signature[CARD_READER_FAT_SIG_LEN] = {'F', 'A', 'T'};
static const char k_card_reader_gpt_signature[CARD_READER_GPT_SIGNATURE_LEN] = {'E', 'F', 'I', ' ', 'P', 'A', 'R', 'T'};
static uint16_t read_le16(const uint8_t *p)
{
    uint16_t value = 0;
    /* Copy bytes into a native value without assuming alignment. */
    memcpy(&value, p, sizeof(value));
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
    /* Convert to host endianness when running on a big-endian CPU. */
    value = (uint16_t)((value >> 8) | (value << 8));
#endif
    return value;
}

/************************************************
* read_le32
* Reads a 32-bit little-endian value from a byte buffer.
* Parameters: p = input byte pointer.
* Returns: 32-bit value.
***************************************************/
static uint32_t read_le32(const uint8_t *p)
{
    uint32_t value = 0;
    /* Copy bytes into a native value without assuming alignment. */
    memcpy(&value, p, sizeof(value));
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
    /* Convert to host endianness when running on a big-endian CPU. */
    value = __builtin_bswap32(value);
#endif
    return value;
}

/************************************************
* read_le64_low32
* Reads the low 32 bits of a 64-bit little-endian value.
* Parameters: p = input byte pointer.
* Returns: low 32-bit value.
***************************************************/
static uint32_t read_le64_low32(const uint8_t *p)
{
    return read_le32(p);
}

/************************************************
* sd_read_block
* Reads a 512-byte sector from the SD card into a buffer.
* Parameters: state = card reader instance, lba = sector index,
*             buf = destination buffer (512 bytes).
* Returns: 1 on success, 0 on failure.
***************************************************/
static uint8_t sd_read_block(card_reader_state_t *state, uint32_t lba, uint8_t *buf)
{
    /* Convert logical block address to byte address for non-SDHC cards. */
    uint32_t addr = state->status.sd_is_sdhc ? lba : (lba << 9);
    for (uint8_t attempt = 0; attempt < CARD_READER_SD_READ_ATTEMPTS; attempt++)
    {
        /* Retry once at a slower bus speed if the fast attempt fails. */
        if (attempt == 0)
        {
            /* Start with fast transfers; retry at slow speed if needed. */
            card_reader_spi_set_speed(CARD_READER_SPI_SPEED_FAST);
        }
        else
        {
            card_reader_spi_set_speed(CARD_READER_SPI_SPEED_SLOW);
        }
        uint8_t r1 = card_reader_spi_send_cmd(17, addr, 0x01, 0, 0);
        if (r1 != CARD_READER_SD_R1_READY)
        {
            /* Command rejected or card busy; release bus and retry. */
            card_reader_spi_deselect();
            continue;
        }
        /* Wait for the data token that precedes the sector payload. */
        uint16_t timeout = CARD_READER_SD_READ_TIMEOUT;
        uint8_t token;
        do
        {
            token = card_reader_spi_transfer_byte(CARD_READER_SD_IDLE_BYTE);
        } 
        while (token == CARD_READER_SD_IDLE_BYTE && --timeout);
        
        if (token != CARD_READER_SD_DATA_TOKEN)
        {
            /* Missing data token; abandon this attempt. */
            card_reader_spi_deselect();
            continue;
        }
        /* Read the full sector payload and discard the CRC bytes. */
        card_reader_spi_read_buffer(buf, CARD_READER_FAT_SECTOR_SIZE);
        uint8_t crc[2];
        card_reader_spi_read_buffer(crc, 2);
        /* Release the SD card and restore fast speed for later operations. */
        card_reader_spi_deselect();
        card_reader_spi_set_speed(CARD_READER_SPI_SPEED_FAST);
        return 1;
    }
    /* All attempts failed; ensure bus speed is restored. */
    card_reader_spi_set_speed(CARD_READER_SPI_SPEED_FAST);
    return 0;
}

/************************************************
* fat32_cluster_to_lba
* Converts a FAT32 cluster index to its starting LBA.
* Parameters: state = card reader instance, cluster = FAT cluster.
* Returns: LBA of the first sector in the cluster.
***************************************************/
static uint32_t fat32_cluster_to_lba(const card_reader_state_t *state, uint32_t cluster)
{
    /* Data region starts at cluster 2 in FAT32. */
    return state->fat32.first_data + (cluster - 2u) * state->fat32.sectors_per_cluster;
}


/************************************************
* card_reader_fat_is_formatted
* Detects a FAT-formatted volume on the SD card.
* Parameters: state = card reader instance.
* Returns: 1 if a FAT signature is found, 0 otherwise.
***************************************************/
uint8_t card_reader_fat_is_formatted(card_reader_state_t *state)
{
    uint8_t sector[CARD_READER_FAT_SECTOR_SIZE];
    /* Read sector 0 to inspect MBR/VBR signatures. */
    if (!sd_read_block(state, 0, sector))
    {
        return 0;
    }
    /* Verify the boot sector signature. */
    if (sector[CARD_READER_MBR_SIG_OFFSET] != CARD_READER_MBR_SIG_BYTE0 ||
        sector[CARD_READER_MBR_SIG_OFFSET + 1] != CARD_READER_MBR_SIG_BYTE1)
    {
        return 0;
    }

    /* Check for a VBR with a FAT signature. */
    if ((sector[0] == CARD_READER_VBR_JMP0 || sector[0] == CARD_READER_VBR_JMP1) &&
        ((memcmp(&sector[CARD_READER_FAT_SIG_OFFSET_FAT12_16],
        k_card_reader_fat_signature,
        CARD_READER_FAT_SIG_LEN) == 0) ||
        (memcmp(&sector[CARD_READER_FAT_SIG_OFFSET_FAT32],
        k_card_reader_fat_signature,
        CARD_READER_FAT_SIG_LEN) == 0)))
    {
        /* Found FAT signature directly in the boot sector. */
        return 1;
    }

    /* If MBR is present, inspect the first partition entry. */
    uint8_t ptype = sector[CARD_READER_MBR_PART_ENTRY0 + CARD_READER_MBR_PART_TYPE_OFFSET];
    if (ptype == CARD_READER_PTYPE_FAT12 ||
       ptype == CARD_READER_PTYPE_FAT16_LT32M ||
       ptype == CARD_READER_PTYPE_FAT16 ||
       ptype == CARD_READER_PTYPE_FAT32_CHS ||
       ptype == CARD_READER_PTYPE_FAT32_LBA ||
       ptype == CARD_READER_PTYPE_FAT16_LBA)
    {
        uint32_t lba = read_le32(&sector[CARD_READER_MBR_PART_ENTRY0 + CARD_READER_MBR_PART_LBA_OFFSET]);
        if (!sd_read_block(state, lba, sector))
        {
            return 0;
        }
        /* Verify the volume boot record signature. */
        if (sector[CARD_READER_MBR_SIG_OFFSET] != CARD_READER_MBR_SIG_BYTE0 ||
           sector[CARD_READER_MBR_SIG_OFFSET + 1] != CARD_READER_MBR_SIG_BYTE1)
        {
            return 0;
        }
        if ((memcmp(&sector[CARD_READER_FAT_SIG_OFFSET_FAT12_16],
            k_card_reader_fat_signature,
            CARD_READER_FAT_SIG_LEN) == 0) ||
            (memcmp(&sector[CARD_READER_FAT_SIG_OFFSET_FAT32],
            k_card_reader_fat_signature,
            CARD_READER_FAT_SIG_LEN) == 0))
        {
            /* Found FAT signature in the partition VBR. */
            return 1;
        }
    }

    /* GPT protective MBR: validate GPT header and then VBR of the first partition. */
    if (ptype == CARD_READER_PTYPE_GPT_PROTECTIVE)
    {
        /* Step 1: read GPT header from LBA1. */
        if (!sd_read_block(state, CARD_READER_GPT_HEADER_LBA, sector))
        {
            return 0;
        }
        /* Step 2: confirm the GPT header signature ("EFI PART"). */
        if (memcmp(sector, k_card_reader_gpt_signature, CARD_READER_GPT_SIGNATURE_LEN) != 0)
        {
            return 0;
        }
        /* Step 3: locate the partition entries table. */
        uint32_t part_lba = read_le64_low32(&sector[CARD_READER_GPT_PART_ENTRIES_LBA_OFFSET]);
        if (!sd_read_block(state, part_lba, sector))
        {
            return 0;
        }
        /* Step 4: read the first partition entry to get its starting LBA. */
        uint32_t first_lba = read_le64_low32(&sector[CARD_READER_GPT_FIRST_PART_LBA_OFFSET]);
        if (!sd_read_block(state, first_lba, sector))
        {
            return 0;
        }
        /* Step 5: verify the VBR signature in the first partition. */
        if (sector[CARD_READER_MBR_SIG_OFFSET] != CARD_READER_MBR_SIG_BYTE0 ||
            sector[CARD_READER_MBR_SIG_OFFSET + 1] != CARD_READER_MBR_SIG_BYTE1)
        {
            return 0;
        }
        /* Step 6: check for a FAT signature in the partition VBR. */
        if ((memcmp(&sector[CARD_READER_FAT_SIG_OFFSET_FAT12_16],
            k_card_reader_fat_signature,
            CARD_READER_FAT_SIG_LEN) == 0) ||
            (memcmp(&sector[CARD_READER_FAT_SIG_OFFSET_FAT32],
            k_card_reader_fat_signature,
            CARD_READER_FAT_SIG_LEN) == 0))
        {
            /* Found FAT signature in the GPT partition VBR. */
            return 1;
        }
    }

    return 0;
}

/************************************************
* card_reader_fat_mount
* Parses FAT32 boot data and initializes FAT fields.
* Parameters: state = card reader instance.
* Returns: 1 on success, 0 on failure.
***************************************************/
uint8_t card_reader_fat_mount(card_reader_state_t *state)
{
    uint8_t sector[CARD_READER_FAT_SECTOR_SIZE];
    uint32_t lba = 0;

    /* Read the first sector and decide whether it is VBR, MBR, or GPT. */
    if (!sd_read_block(state, 0, sector))
    {
        return 0;
    }

    /* If an MBR signature is present, decide how to locate the VBR. */
    if (sector[CARD_READER_MBR_SIG_OFFSET] == CARD_READER_MBR_SIG_BYTE0 &&
        sector[CARD_READER_MBR_SIG_OFFSET + 1] == CARD_READER_MBR_SIG_BYTE1)
    {
        uint8_t ptype = sector[CARD_READER_MBR_PART_ENTRY0 + CARD_READER_MBR_PART_TYPE_OFFSET];
        /* MBR: follow the first partition entry when present. */
        if (ptype == CARD_READER_PTYPE_FAT32_CHS ||
            ptype == CARD_READER_PTYPE_FAT32_LBA ||
            ptype == CARD_READER_PTYPE_FAT16_LBA ||
            ptype == CARD_READER_PTYPE_FAT16 ||
            ptype == CARD_READER_PTYPE_FAT12 ||
            ptype == CARD_READER_PTYPE_FAT16_LT32M)
        {
            /* FAT partition type: jump to its VBR using the start LBA. */
            lba = read_le32(&sector[CARD_READER_MBR_PART_ENTRY0 + CARD_READER_MBR_PART_LBA_OFFSET]);
            if (!sd_read_block(state, lba, sector))
            {
                return 0;
            }
        } 
        else if (ptype == CARD_READER_PTYPE_GPT_PROTECTIVE)
        {
            /* GPT: follow header to locate the first partition LBA. */
            if (!sd_read_block(state, CARD_READER_GPT_HEADER_LBA, sector))
            {
                return 0;
            }
            /* Validate GPT header signature before reading GPT fields. */
            if (memcmp(sector, k_card_reader_gpt_signature, CARD_READER_GPT_SIGNATURE_LEN) != 0)
            {
                return 0;
            }
            uint32_t part_lba = read_le64_low32(&sector[CARD_READER_GPT_PART_ENTRIES_LBA_OFFSET]);
            if (!sd_read_block(state, part_lba, sector))
            {
                return 0;
            }
            lba = read_le64_low32(&sector[CARD_READER_GPT_FIRST_PART_LBA_OFFSET]);
            if (!sd_read_block(state, lba, sector))
            {
                return 0;
            }
        }
    }

    /* Validate VBR signature before reading BPB fields. */
    /* If the VBR signature is missing, we cannot parse FAT fields. */
    if (sector[CARD_READER_MBR_SIG_OFFSET] != CARD_READER_MBR_SIG_BYTE0 ||
        sector[CARD_READER_MBR_SIG_OFFSET + 1] != CARD_READER_MBR_SIG_BYTE1)
    {
        return 0;
    }

    /* Parse BPB fields needed for FAT32 layout. */
    uint16_t bytes_per_sector = read_le16(&sector[CARD_READER_BPB_BYTES_PER_SECTOR_OFFSET]);
    /* Require 512-byte sectors, which this driver assumes. */
    if (bytes_per_sector != CARD_READER_FAT_SECTOR_SIZE)
    {
        return 0;
    }
    state->fat32.sectors_per_cluster = sector[CARD_READER_BPB_SECTORS_PER_CLUSTER_OFFSET];
    uint16_t reserved = read_le16(&sector[CARD_READER_BPB_RESERVED_SECTORS_OFFSET]);
    uint8_t num_fats = sector[CARD_READER_BPB_NUM_FATS_OFFSET];
    state->fat32.fat_size = read_le32(&sector[CARD_READER_BPB_FAT_SIZE_OFFSET]);
    state->fat32.root_cluster = read_le32(&sector[CARD_READER_BPB_ROOT_CLUSTER_OFFSET]);

    /* Reject invalid or uninitialized FAT32 fields. */
    if (state->fat32.sectors_per_cluster == 0 || state->fat32.fat_size == 0 || state->fat32.root_cluster < 2)
    {
        return 0;
    }

    /* Cache derived LBA positions for FAT and data regions. */
    state->fat32.lba_start = lba;
    state->fat32.first_fat = state->fat32.lba_start + reserved;
    state->fat32.first_data = state->fat32.lba_start + reserved + (uint32_t)num_fats * state->fat32.fat_size;
    state->status.sd_fat32_ready = 1;
    return 1;
}

/************************************************
* fat32_read_fat
* Reads the FAT entry for a cluster.
* Parameters: state = card reader instance, cluster = FAT cluster,
*             sector = scratch buffer.
* Returns: FAT entry value (masked), or end marker on failure.
***************************************************/
static uint32_t fat32_read_fat(card_reader_state_t *state, uint32_t cluster, uint8_t *sector)
{
    /* Each FAT32 entry is 4 bytes; compute sector and offset. */
    uint32_t fat_offset = cluster * 4u;
    uint32_t fat_sector = state->fat32.first_fat + (fat_offset / 512u);
    uint16_t fat_index = (uint16_t)(fat_offset % 512u);
    if (!sd_read_block(state, fat_sector, sector))
    {
        return CARD_READER_FAT_ENTRY_MASK;
    }
    uint32_t entry = (uint32_t)sector[fat_index] |
    ((uint32_t)sector[fat_index + 1] << 8) |
    ((uint32_t)sector[fat_index + 2] << 16) |
    ((uint32_t)sector[fat_index + 3] << 24);
    return entry & CARD_READER_FAT_ENTRY_MASK;
}

/************************************************
* fat32_name_to_83
* Converts a filename to the 8.3 padded uppercase format.
* Parameters: name = input name, out = 11-byte output buffer.
* Returns: void.
***************************************************/
static void fat32_name_to_83(const char *name, char out[CARD_READER_FAT_NAME83_LEN])
{
    /* Fill with spaces, then copy name and extension in uppercase 8.3 format. */
    for (uint8_t i = 0; i < CARD_READER_FAT_NAME83_LEN; i++)
    {
        out[i] = ' ';
    }
    uint8_t i = 0;
    while (*name && *name != '.' && i < 8)
    {
        char c = *name++;
        if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
        out[i++] = c;
    }
    if (*name == '.')
    {
        name++;
        i = 8;
        while (*name && i < CARD_READER_FAT_NAME83_LEN)
        {
            char c = *name++;
            if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
            out[i++] = c;
        }
    }
}

/************************************************
* card_reader_fat_find_file_root
* Finds a file in the root directory and returns its metadata.
* Parameters: state = card reader instance, name = filename,
*             first_cluster = output start cluster,
*             file_size = output size in bytes.
* Returns: 1 if found, 0 otherwise.
***************************************************/
uint8_t card_reader_fat_find_file_root(card_reader_state_t *state,
const char *name,
uint32_t *first_cluster,
uint32_t *file_size)
{
    if (!state->status.sd_fat32_ready)
    {
        return 0;
    }
    /* Convert input name to 8.3 for directory matching. */
    char name83[CARD_READER_FAT_NAME83_LEN];
    fat32_name_to_83(name, name83);

    uint8_t sector[CARD_READER_FAT_SECTOR_SIZE];
    uint32_t cluster = state->fat32.root_cluster;
    /* Walk directory clusters until end-of-chain. */
    while (cluster < CARD_READER_FAT_EOC_MARKER)
    {
        uint32_t lba = fat32_cluster_to_lba(state, cluster);
        for (uint8_t s = 0; s < state->fat32.sectors_per_cluster; s++)
        {
            if (!sd_read_block(state, lba + s, sector))
            {
                return 0;
            }
            /* Each directory entry is 32 bytes. */
            for (uint16_t off = 0; off < CARD_READER_FAT_SECTOR_SIZE; off += CARD_READER_FAT_DIR_ENTRY_SIZE)
            {
                uint8_t first = sector[off];
                /* 0x00 marks the end of active directory entries. */
                if (first == CARD_READER_FAT_DIR_ENTRY_FREE)
                {
                    /* End of directory entries. */
                    return 0;
                }
                /* 0xE5 marks a deleted entry. */
                if (first == CARD_READER_FAT_DIR_ENTRY_DELETED)
                {
                    /* Deleted entry. */
                    continue;
                }
                uint8_t attr = sector[off + CARD_READER_FAT_DIR_ATTR_OFFSET];
                /* Skip long filename entries; only 8.3 entries are used here. */
                if (attr == CARD_READER_FAT_DIR_ATTR_LFN)
                {
                    /* Skip long filename entries. */
                    continue;
                }
                uint8_t match = 1;
                for (uint8_t i = 0; i < CARD_READER_FAT_NAME83_LEN; i++)
                {
                    if ((char)sector[off + i] != name83[i])
                    {
                        match = 0;
                        break;
                    }
                }
                /* If all 11 characters match, this is the target entry. */
                if (match)
                {
                    /* Combine high/low cluster parts and read size. */
                    uint16_t hi = (uint16_t)sector[off + CARD_READER_FAT_DIR_CLUSTER_HI_OFFSET] |
                    ((uint16_t)sector[off + CARD_READER_FAT_DIR_CLUSTER_HI_OFFSET + 1] << 8);
                    uint16_t lo = (uint16_t)sector[off + CARD_READER_FAT_DIR_CLUSTER_LO_OFFSET] |
                    ((uint16_t)sector[off + CARD_READER_FAT_DIR_CLUSTER_LO_OFFSET + 1] << 8);
                    *first_cluster = ((uint32_t)hi << 16) | lo;
                    *file_size = (uint32_t)sector[off + CARD_READER_FAT_DIR_FILE_SIZE_OFFSET] |
                    ((uint32_t)sector[off + CARD_READER_FAT_DIR_FILE_SIZE_OFFSET + 1] << 8) |
                    ((uint32_t)sector[off + CARD_READER_FAT_DIR_FILE_SIZE_OFFSET + 2] << 16) |
                    ((uint32_t)sector[off + CARD_READER_FAT_DIR_FILE_SIZE_OFFSET + 3] << 24);
                    return 1;
                }
            }
        }
        cluster = fat32_read_fat(state, cluster, sector);
    }
    return 0;
}

/************************************************
* card_reader_fat_read_file_stream_ctx
* Streams file contents to a sink callback.
* Parameters: state = card reader instance, name = filename,
*             expected_size = 0 to ignore size or exact size,
*             sink = callback for data chunks, ctx = user context.
* Returns: 1 on success, 0 on failure.
***************************************************/
uint8_t card_reader_fat_read_file_stream_ctx(card_reader_state_t *state,
const char *name,
uint32_t expected_size,
void (*sink)(const uint8_t *data, uint16_t len, void *ctx),
void *ctx)
{
    uint32_t first_cluster = 0;
    uint32_t file_size = 0;
    if (!card_reader_fat_find_file_root(state, name, &first_cluster, &file_size))
    {
        return 0;
    }
    if (expected_size && file_size != expected_size)
    {
        return 0;
    }

    uint8_t sector[CARD_READER_FAT_SECTOR_SIZE];
    uint32_t cluster = first_cluster;
    uint32_t remaining = file_size;
    /* Stream clusters in order until EOF. */
    while (cluster < CARD_READER_FAT_EOC_MARKER && remaining)
    {
        uint32_t lba = fat32_cluster_to_lba(state, cluster);
        for (uint8_t s = 0; s < state->fat32.sectors_per_cluster && remaining; s++)
        {
            if (!sd_read_block(state, lba + s, sector))
            {
                return 0;
            }
            /* Send one sector (or remaining bytes) to the sink. */
            uint16_t chunk = remaining > CARD_READER_FAT_SECTOR_SIZE
            ? CARD_READER_FAT_SECTOR_SIZE
            : (uint16_t)remaining;
            sink(sector, chunk, ctx);
            remaining -= chunk;
        }
        cluster = fat32_read_fat(state, cluster, sector);
    }
    return remaining == 0;
}
