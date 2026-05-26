/**
 * @file custom.h
 * @brief IMUTagBreakout variant build constants and legacy line aliases.
 * @author tag firmware authors
 * @date 2026-05-23
 */

#ifndef CUSTOM_H
#define CUSTOM_H

/** @name Storage and firmware identity
 * External flash sizing, strings, protocol sizing, and feature flags.
 * @{
 */
#define EXT_FLASH_SIZE (1024 * 1024 * 4)

#define FIRMWARE_STRING "IMUTagBreakoutv1, Firmware version 1"
#undef  BOARD_NAME
#define BOARD_NAME "IMUTagBreakoutv1"
#define USE_LPS22HH 1
#define SENSOR_CALIBRATION 1
#define QTMONITOR_VERSION 2.0
#define PROTOBUFSIZE 4096
#define SENSOR_CONSTANTS 1
#define CALIBRATION_CONSTANTS 1
/** @} */

/** @name Sensor line aliases and conversion constants
 * Board-level wake lines and sample scale constants used by local code.
 * @{
 */
#define LINE_ACCEL_INT LINE_WKUP1

#define ACCEL_CONSTANT 0.976f
#define MAG_CONSTANT 0.01
#define SWAP_I2C 1
/** @} */

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
