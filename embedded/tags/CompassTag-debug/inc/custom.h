#ifndef CUSTOM_H
#define CUSTOM_H

#define EXT_FLASH_SIZE (1024 * 1024 * 4)

#define FIRMWARE_STRING "CompassTagv1, Firmware version 1"
#undef  BOARD_NAME
#define BOARD_NAME "CompassTagv1"
#define RV3028_RTC TRUE
#define USE_LIS2DU12 TRUE
#define ACCEL_USART TRUE
#define USE_AK09940A TRUE
#define EXTERNAL_FLASH TRUE
#define SENSOR_CALIBRATION TRUE
#define QTMONITOR_VERSION 2.0
#define PROTOBUFSIZE 4096
#define SENSOR_CONSTANTS TRUE
#define CALIBRATION_CONSTANTS TRUE;

extern volatile int sectors_erased;

#define LINE_ACCEL_INT LINE_WKUP1

#define ACCEL_CONSTANT 0.976f
#define MAG_CONSTANT 0.01

#include "lis2du12.h"

#endif