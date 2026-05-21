#ifndef CUSTOM_H
#define CUSTOM_H

#define EXT_FLASH_SIZE (1024 * 1024 * 4)

#define FIRMWARE_STRING "IMUTagBreakoutv1, Firmware version 1"
#undef  BOARD_NAME
#define BOARD_NAME "IMUTagBreakoutv1"
#define USE_LPS22HH TRUE
#define SENSOR_CALIBRATION TRUE
#define QTMONITOR_VERSION 2.0
#define PROTOBUFSIZE 4096
#define SENSOR_CONSTANTS TRUE
#define CALIBRATION_CONSTANTS TRUE;

#define LINE_ACCEL_INT LINE_WKUP1

#define ACCEL_CONSTANT 0.976f
#define MAG_CONSTANT 0.01
#define SWAP_I2C TRUE

/*
 * The IMUTagBreakout board file predates the standard magnetometer line names
 * used by shared power code. Keep the aliases local to this tag; update the
 * board file to emit LINE_MAG_* directly if this target is revived for more
 * active development.
 */
#define LINE_MAG_CS LINE_AK_CS
#define LINE_MAG_SCK LINE_LSM_CK
#define LINE_MAG_MISO LINE_LPS_MISO
#define LINE_MAG_MOSI LINE_LSM_MOSI
#define LINE_MAG_TRG LINE_AK_TRG

/*
 * The LPS22HH shares the same SPI clock as the LSM device on this board.
 * The shared pressure shim uses standard LINE_LPS_* names, so keep the alias
 * here with the rest of the legacy board-name compatibility.
 */
#define LINE_LPS_SCK LINE_LSM_CK

#endif
