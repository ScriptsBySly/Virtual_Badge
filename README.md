# Virtual_Badge
Code to display PNGs to a small display using an ATmega328P

## Build
Requires `avr-gcc`, `avr-libc`, `cmake`, and `python3`.

- Build: `./Build`
- Flash: `cmake --build build --target flash`

## Images
Images are generated at build time from `HD_EO_MC.png` and `HU_EO_MC.png` into `build/generated/generated_images.h` using `tools/png_to_rgb565.py`.

The generator currently:
- Resizes with nearest-neighbor using a “contain” fit into `128x160`
- Emits RGB565 with simple RLE compression for AVR flash size

## SD Card (SPI)
Wiring for Arduino Nano (ATmega328P):
- `SCK` → `D13`
- `MOSI` → `D11`
- `MISO` → `D12`
- `CS` → `D4`
- `VCC` → `3.3V` (or `5V` only if the module has a regulator + level shifting)
- `GND` → `GND`

Files must be FAT32 (exFAT is not supported). Place images in the root of the card.

This firmware reads raw RGB565 files, not PNG. Use the tool below to generate `.RAW` files.

Generate raw files (128x160, RGB565 big-endian):
```bash
python3 tools/png_to_rgb565.py --out /tmp/ignore.h --resize 128x160 --raw-dir /tmp/raw HD_EO_MC.png HU_EO_MC.png
```
Copy `/tmp/raw/IMG_HD_EO_MC.RAW` to the SD card as `HD_EO_MC.RAW` (same for `HU_EO_MC.RAW`).

CLI on UART @ 9600 8N1:
- `s` = status
- `r` = read LBA0 (prints signature + first 16 bytes)
- `p` = play SD images
