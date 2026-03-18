# Card Reader Software Design

## Overview
The card reader subsystem provides SD card access over SPI and a minimal FAT32 reader that streams raw RGB565 image files to the display. It is designed for small embedded targets with limited RAM and no filesystem library, and prioritizes deterministic behavior over feature completeness.

Key responsibilities:
- Initialize and probe SD card over SPI (SDSC/SDHC/SDXC).
- Parse FAT32 structures sufficiently to locate files in the root directory.
- Stream file data (512‑byte sector reads) to the display.
- Optionally cache image files in RAM on ESP platforms.
- Provide debug/diagnostic info through UART‑style logging and query APIs.

Primary code:
- Implementation: `src/card_reader.c`
- API: `src/card_reader.h`
- Display sink: `src/display.c`, `src/display.h`
- Hardware abstraction: `src/hal/hal.h` and HAL implementations.

## Goals
- Read raw RGB565 files from SD in SPI mode.
- Support FAT32 root directory (short 8.3 filenames).
- Stream file data efficiently to the display without full file buffering.
- Provide enough diagnostics to debug SD/FAT failures without a full filesystem.

## Non‑Goals
- Full FAT32 support (subdirectories, LFN, write operations).
- exFAT, NTFS, or other filesystems.
- Robust recovery from all SD card faults or hot‑swap.
- DMA‑optimized or multi‑threaded pipeline.

## High‑Level Architecture
```
SD (SPI) -> SD SPI protocol -> Sector reads (512B) -> FAT32 parser -> File data stream
                                                              \-> Debug/Status info
File data stream -> display_stream_bytes() -> TFT over SPI
```

Modules:
- **SD Protocol (SPI)**: low‑level command/response (CMD0, CMD8, ACMD41, CMD58, CMD17).
- **FAT32**: boot sector parsing, FAT traversal, root directory scan.
- **Image Streaming**: send file contents directly to TFT, optionally caching.
- **Diagnostics**: UART logs and status getters.

## Data Flow
1. **Initialization** (`card_reader_file_open`):
   - `sd_init()` performs SPI init, idle clocks, CMD0/CMD8/ACMD41/CMD58 sequence.
   - `sd_is_fat_formatted()` reads MBR/VBR to confirm FAT signature.
   - `fat32_mount()` parses FAT32 parameters and sets globals.
2. **Render** (`card_reader_draw_raw565`):
   - If cache enabled and image cached: stream from RAM.
   - Otherwise: `fat32_find_file_root` -> cluster chain -> `fat32_read_file_stream`.
   - `fat32_read_file_stream` reads sectors and feeds them to a sink:
     - `tft_stream_bytes_sd` (direct to display) or
     - `tft_stream_bytes_cache` (copy to cache + stream).

## SD SPI Protocol Design
### Initialization Sequence
- 160 idle clocks with CS high.
- CMD0 to enter idle.
- CMD8 to detect SD v2.
- ACMD41 loop with HCS bit for SDHC/SDXC.
- CMD58 to read OCR and check SDHC bit.
- Fallback for SD v1/MMC: ACMD41 without HCS and CMD1 if needed.

### Read Path
`sd_read_block(lba, buf)`:
- Sends CMD17 (READ_SINGLE_BLOCK).
- Waits for data token 0xFE.
- Reads 512 bytes + 2‑byte CRC.
- Retries once with slower SPI if needed.

### Key SD State
- `sd_is_sdhc`: determines byte vs block address for CMD17.
- `sd_fat32_ready`: indicates FAT parameters parsed and ready.
- Command response snapshots (`sd_last_cmd*`) for diagnostics.

## FAT32 Minimal Parser
### Boot Sector / MBR
`sd_is_fat_formatted()`:
- Checks MBR/VBR signatures.
- For MBR, reads partition entry type and loads VBR.
- For GPT, looks up first partition entry.
- Validates FAT signatures in BPB.

### Mounting
`fat32_mount()` extracts:
- `fat32_sectors_per_cluster`
- `fat32_first_fat`
- `fat32_first_data`
- `fat32_fat_size`
- `fat32_root_cluster`

### File Lookup
`fat32_find_file_root(name)`:
- Converts to 8.3 uppercase (no LFN).
- Scans root directory cluster chain.
- Returns first cluster + file size.

### Streaming
`fat32_read_file_stream_ctx()`:
- Walks cluster chain via FAT reads.
- Reads each sector and passes data to a sink callback.
- Does not allocate file‑sized buffers.

## Image Cache (ESP Platforms)
Enabled when `ESP_PLATFORM` is defined.
- LRU‑like cache with fixed slots (`IMAGE_CACHE_SLOTS`).
- Each entry stores filename, size, data pointer, last‑use tick.
- Cache fill uses streaming with optional display output.
- Cache size and allocation strategy are simple and non‑fragmenting.

## API Surface
Public APIs in `src/drivers/card_reader.h`:
- `card_reader_file_open()`: Initialize SD/FAT and return a device instance.
- `card_reader_file_read(...)`: Stream file contents via a sink callback.
- `card_reader_file_write(...)`: File write (currently stubbed).
- `card_reader_file_close(...)`: Reset state and release resources.

## Configuration & Build‑Time Controls
- `ESP_PLATFORM`: enables cache and ESP behaviors.
- `IMAGE_CACHE_SLOTS`: fixed number of cache entries.
- SPI speeds are controlled by HAL:
  - SD init uses slow SPI; after init uses fast.
  - TFT uses fast SPI by default.

## Error Handling Strategy
- Functions return `0/1` for failure/success.
- SD errors are surfaced via UART prints and diagnostic getters.
- FAT errors are handled by returning failure and optionally rendering a debug screen.

## Concurrency and Reentrancy
- Not thread‑safe.
- Global state is used for SD/FAT and cache.
- Intended for single‑threaded main loop.

## Performance Characteristics
- Single‑sector reads (512 bytes) with optional retry.
- Direct streaming minimizes memory usage.
- Cache avoids repeated SD reads for frequently used images.

## Known Limitations
- Root directory only, 8.3 names only.
- No write support.
- No dynamic memory for file buffering besides cache.
- FAT32 only (no exFAT).
- Uses busy‑wait loops for SD response.

## Suggested Refactors for Long‑Term Maintainability
1. **Separate SD, FAT32, and Image streaming** into distinct translation units.
2. **Introduce explicit error types** instead of `0/1`.
3. **Abstract sink interface** (display vs cache) into a formal struct with function pointers.
4. **Move diagnostics into a dedicated module** or compile‑time feature.
5. **Add unit tests** for FAT parsing and name conversion using host builds.
6. **Make cache size configurable** and use fixed‑size allocator to avoid fragmentation.
7. **Add bounds checks** for file sizes vs expected image dimensions.
8. **Consider LFN support** if asset names grow.

## Test Plan Ideas
- SD init on SDSC vs SDHC cards.
- FAT32 with MBR and with GPT protective MBR.
- Missing file -> error screen/return false.
- Cache hit/miss behavior and eviction order.
- SPI error injection (bad token, no response).

## Appendix: Key Files
- `src/card_reader.c`: SD/FAT32 + image streaming implementation.
- `src/card_reader.h`: public API.
- `src/display.c`: display writes and text primitives.
- `src/hal/hal.h`: SPI/UART HAL interface.
