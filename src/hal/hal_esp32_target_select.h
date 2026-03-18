#pragma once

// Select target defaults based on build target. Override by defining HAL_... macros.

#if defined(CONFIG_IDF_TARGET_ESP32S3) || defined(ESP32S3) || defined(ARDUINO_ESP32S3_DEV)
#include "hal_esp32_target_esp32s3.h"
#else
#include "hal_esp32_target_esp32.h"
#endif
