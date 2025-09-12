#ifndef CUSTOM_H
#define CUSTOM_H

#define EXT_FLASH_SIZE (1024 * 1024 * 4)

#define FIRMWARE_STRING "CompassTagv1, Firmware version 1"
#undef  BOARD_NAME
#define BOARD_NAME "CompassTagv1"
#define RV3028_RTC TRUE
#define USE_LPS27 TRUE
#define USE_LIS2DU12 TRUE
#define USE_AK9940A TRUE
#define EXTERNAL_FLASH TRUE
#define QTMONITOR_VERSION 2.0
#define PROTOBUFSIZE 4096

extern volatile int sectors_erased;

#define LINE_ACCEL_INT LINE_WKUP1

#endif