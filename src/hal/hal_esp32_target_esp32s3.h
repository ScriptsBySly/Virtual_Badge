#pragma once

// Default pinout + SPI host for ESP32-S3. Override via -DHAL_... defines.

#ifndef HAL_ESP32_TFT_SPI_HOST
#define HAL_ESP32_TFT_SPI_HOST SPI2_HOST
#endif
#ifndef HAL_ESP32_SD_SPI_HOST
#define HAL_ESP32_SD_SPI_HOST SPI3_HOST
#endif

#ifndef HAL_ESP32_SPI_SCK
#define HAL_ESP32_SPI_SCK 12
#endif
#ifndef HAL_ESP32_SPI_MISO
#define HAL_ESP32_SPI_MISO 13
#endif
#ifndef HAL_ESP32_SPI_MOSI
#define HAL_ESP32_SPI_MOSI 11
#endif

#ifndef HAL_ESP32_SD_SPI_SCK
#define HAL_ESP32_SD_SPI_SCK 6
#endif
#ifndef HAL_ESP32_SD_SPI_MISO
#define HAL_ESP32_SD_SPI_MISO 4
#endif
#ifndef HAL_ESP32_SD_SPI_MOSI
#define HAL_ESP32_SD_SPI_MOSI 5
#endif

#ifndef HAL_ESP32_TFT_CS
#define HAL_ESP32_TFT_CS 10
#endif
#ifndef HAL_ESP32_TFT_DC
#define HAL_ESP32_TFT_DC 9
#endif
#ifndef HAL_ESP32_TFT_RST
#define HAL_ESP32_TFT_RST 8
#endif

#ifndef HAL_ESP32_SD_CS
#define HAL_ESP32_SD_CS 7
#endif
