#ifndef CUSTOM_H
#define CUSTOM_H

#define EXT_FLASH_SIZE (1024 * 1024 * 4)

#define FIRMWARE_STRING "BitPresTagv4, Firmware version 1"
#undef  BOARD_NAME
#define BOARD_NAME "BitPresTagv1"
#define RV3028_RTC TRUE
#define LPS_USART TRUE
#define LPS_LOW_POWER FALSE
#define USE_LPS27 TRUE
#define USE_ADXL362 TRUE
#define EXTERNAL_FLASH TRUE
#define QTMONITOR_VERSION 2.0
#define PROTOBUFSIZE 4096

extern volatile int sectors_erased;
#define LINE_STEVAL_CS LINE_LPS_CS
#define LINE_ACCEL_INT LINE_WKUP1

#define STM32_MSIRANGE_FAST STM32_MSIRANGE_24M
#define RANGE_MULTIPLIER 12
#define FLASH_WS_SLOW 0
#define FLASH_WS_FAST 3

#endif