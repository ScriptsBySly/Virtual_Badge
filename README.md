# Virtual_Badge
Code to display RGB565 animations on a small SPI TFT with SD storage.

## Build
Requires `python3`.

### AVR (Arduino Nano)
Requires `avr-gcc`, `avr-libc`, and `cmake`.

- Build: `./Build -t nano`
- Flash: `cmake --build build --target flash` (uses `scripts/flash_nano.sh`)
- Flash (manual): `scripts/flash_nano.sh -d /dev/ttyUSB0 output/virtual_badge_avr.hex`

### ESP32 (ESP-IDF)
Requires ESP-IDF installed and `export.sh` sourced so `IDF_PATH` is set.

- Configure target (once): `idf.py set-target esp32`
- Build: `./Build -t esp32`
- Flash (esptool): `scripts/flash_esp32.sh -d /dev/ttyUSB0`

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

### ESP32 (KeeYees ESP32 OLED WiFi Kit, CP2102)
HAL file (ESP-IDF): `src/hal/hal_esp32_idf.c`

**Default SPI pins (VSPI)**
- `SCK` → GPIO18
- `MOSI` → GPIO23
- `MISO` → GPIO19

**Default control pins (overridable)**
- `TFT_CS` → GPIO16
- `TFT_DC` → GPIO17
- `TFT_RST` → GPIO25
- `SD_CS` → GPIO27

**On‑board OLED**
- Typically uses I2C (`SDA` GPIO21, `SCL` GPIO22). Avoid those pins for SPI devices. citeturn0search1turn0search3

**Build selection**
- Set `HAL_MCU=esp32` to use the ESP32 HAL and `HAL_MCU=atmega328p` for Nano.

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
