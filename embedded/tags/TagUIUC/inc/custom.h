#ifndef _CUSTOM_H_
#define _CUSTOM_H_

/* Firmware identity */
#define FIRMWARE_STRING "TagUIUC, Firmware version 1.0.0"
#undef  BOARD_NAME
#define BOARD_NAME "TagUIUC"
#define SWAP_I2C 1
#define QTMONITOR_VERSION 2.0

/* Monitor protobuf buffer size in bytes */
#define PROTOBUFSIZE 4096

/* Sleep mode policy */
#define TAG_IDLE_SLEEP_MODE SHUTDOWN
#define TAG_CONFIGURED_SLEEP_MODE SHUTDOWN
#define TAG_HIBERNATING_SLEEP_MODE SHUTDOWN
#define TAG_FINISHED_SLEEP_MODE SHUTDOWN
#define TAG_ABORTED_SLEEP_MODE SHUTDOWN

/* Fast-clock policy */
#define STM32_MSIRANGE_FAST STM32_MSIRANGE_24M
#define RANGE_MULTIPLIER 12
#define FLASH_WS_SLOW 0
#define FLASH_WS_FAST 3

#endif