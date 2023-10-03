#ifndef CUSTOM_H
#define CUSTOM_H

#define EXT_FLASH_SIZE (1024 * 1024 * 4)

#define FIRMWARE_STRING "BitTagNG, Firmware version 1"
#undef  BOARD_NAME
#define BOARD_NAME "BitTagNG_Steval"
#define RV3028_RTC TRUE
#define ADXL362 TRUE
#define EXTERNAL_FLASH TRUE

#define LINE_ACCEL_CS LINE_ADXL_CS

#endif