#ifndef CUSTOM_H
#define CUSTOM_H

#define EXT_FLASH_SIZE (1024 * 1024 * 4)

#define FIRMWARE_STRING "PresTagv4, Firmware version 1"
#undef  BOARD_NAME
#define BOARD_NAME "PresTagv3"
#define LPS_SPI TRUE
#define LPS_LOW_POWER FALSE
#define QTMONITOR_VERSION 2.0
#define PROTOBUFSIZE 4096

extern volatile int sectors_erased;

#endif
