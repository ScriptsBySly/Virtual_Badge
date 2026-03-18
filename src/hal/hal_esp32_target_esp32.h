#pragma once

// Default pinout + SPI host for ESP32 (non-S3). Override via -DHAL_... defines.

#ifndef HAL_ESP32_TFT_SPI_HOST
#define HAL_ESP32_TFT_SPI_HOST VSPI_HOST
#endif
#ifndef HAL_ESP32_SD_SPI_HOST
#define HAL_ESP32_SD_SPI_HOST VSPI_HOST
#endif

#ifndef HAL_HAS_SEPARATE_SPI_BUSES
#if HAL_ESP32_TFT_SPI_HOST != HAL_ESP32_SD_SPI_HOST
#define HAL_HAS_SEPARATE_SPI_BUSES 1
#else
#define HAL_HAS_SEPARATE_SPI_BUSES 0
#endif
#endif

#ifndef HAL_ESP32_SPI_SCK
#define HAL_ESP32_SPI_SCK 18
#endif
#ifndef HAL_ESP32_SPI_MISO
#define HAL_ESP32_SPI_MISO 19
#endif
#ifndef HAL_ESP32_SPI_MOSI
#define HAL_ESP32_SPI_MOSI 23
#endif

#ifndef HAL_ESP32_SD_SPI_SCK
#define HAL_ESP32_SD_SPI_SCK HAL_ESP32_SPI_SCK
#endif
#ifndef HAL_ESP32_SD_SPI_MISO
#define HAL_ESP32_SD_SPI_MISO HAL_ESP32_SPI_MISO
#endif
#ifndef HAL_ESP32_SD_SPI_MOSI
#define HAL_ESP32_SD_SPI_MOSI HAL_ESP32_SPI_MOSI
#endif

#ifndef HAL_ESP32_TFT_CS
#define HAL_ESP32_TFT_CS 16
#endif
#ifndef HAL_ESP32_TFT_DC
#define HAL_ESP32_TFT_DC 17
#endif
#ifndef HAL_ESP32_TFT_RST
#define HAL_ESP32_TFT_RST 25
#endif

#ifndef HAL_ESP32_SD_CS
#define HAL_ESP32_SD_CS 27
#endif
