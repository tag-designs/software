#ifndef CUSTOM_H
#define CUSTOM_H
#include "board.h"

#define EXT_FLASH_SIZE (1024 * 1024 * 4)

#define FIRMWARE_STRING "BitTagNG-LIS2DU12, Firmware version 1"
#define RV3028_RTC TRUE
#define USE_LIS2DU12 TRUE
#define EXTERNAL_FLASH TRUE
#endif

#define LINE_ACCEL_CS LINE_STEVAL_CS
#define LINE_ACCEL_INT LINE_INT1

extern volatile int sectors_erased;