#ifndef CUSTOM_H
#define CUSTOM_H

#define EXT_FLASH_SIZE (1024 * 1024 * 4)

#define FIRMWARE_STRING "PresTagv4, Firmware version 1"
#undef  BOARD_NAME
#define BOARD_NAME "PresTagv3"
#define RV3028_RTC TRUE
#define LPS_SPI TRUE
#define LPS_LOW_POWER FALSE
#define USE_LPS27 TRUE
#define EXTERNAL_FLASH TRUE
#define QTMONITOR_VERSION 2.0

extern volatile int sectors_erased;

#endif