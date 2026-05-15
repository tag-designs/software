#ifndef CUSTOM_H
#define CUSTOM_H
#include "board.h"

#define EXT_FLASH_SIZE (1024 * 1024 * 4)

#define FIRMWARE_STRING "BitTagNG, Firmware version 1"
#define RV3028_RTC TRUE
#define USE_ADXL367 TRUE
#define EXTERNAL_FLASH TRUE
#endif

extern volatile int sectors_erased;