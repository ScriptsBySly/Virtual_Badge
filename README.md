# Virtual_Badge
Code to display RGB565 animations on a small SPI TFT with SD storage.

## Build
Requires `avr-gcc`, `avr-libc`, `cmake`, and `python3`.

- Build: `./Build`
- Flash: `cmake --build build --target flash`

## Images
This firmware reads **raw RGB565** files from storage (not PNG). Use:

- `tools/png_to_rgb565.py` to generate `.RAW` files (128x160, RGB565 big‑endian)
- `tools/composite_overlays.py` to pre‑compose overlay PNGs into event RAWs

## MCU Profiles
The MCU‑specific code lives in `src/hal/` so you can swap platforms by changing a single file.

### ATmega328P (Arduino Nano)
HAL file: `src/hal/hal_atmega328p.c`

**SPI (shared TFT + SD)**
- `SCK` → `D13` (PB5)
- `MOSI` → `D11` (PB3)
- `MISO` → `D12` (PB4)

**TFT control**
- `TFT_CS` → `D10` (PB2)
- `TFT_DC` → `D9` (PB1)
- `TFT_RST` → `D8` (PB0)

**SD card CS**
- `SD_CS` → `D4` (PD4)

**Power**
- SD slot is usually **3.3V** (level shifting required on 5V boards if not included)

## SD Card (SPI)
Files must be FAT32 (exFAT is not supported). Place images in the root of the card.

**Base animation files (examples)**
- `HUEOMC.RAW`, `HDEOMC.RAW`
- `HUECMC.RAW`, `HDECMC.RAW`
- `HUEMMC.RAW`, `HDEMMC.RAW`

**Event files (pre‑composed)**
- `HUHAPPY.RAW`, `HDHAPPY.RAW`
- `HUSADNEW.RAW`, `HDSADNEW.RAW`
- `HUMAD.RAW`, `HDMAD.RAW`

CLI on UART @ 9600 8N1 (if enabled):
- `s` = status
- `r` = read LBA0 (prints signature + first 16 bytes)
- `l` = list root directory
- `d` = SD probe
