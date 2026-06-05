/**
 * @file custom.h
 * @brief IMUTagBreakout variant build constants.
 * @author tag firmware authors
 * @date 2026-05-23
 */

#ifndef CUSTOM_H
#define CUSTOM_H

/** @name Storage and firmware identity
 * External flash sizing, strings, protocol sizing, and feature flags.
 * @{
 */
#define EXT_FLASH_SIZE (1024 * 1024 * 16)

#define FIRMWARE_STRING "IMUTagBreakoutv1, Firmware version 1"
#undef  BOARD_NAME
#define BOARD_NAME "IMUTagBreakoutv1"
#define USE_LPS22HH 1
#define SENSOR_CALIBRATION 1
#define QTMONITOR_VERSION 2.0
#define PROTOBUFSIZE 4096
#define SENSOR_CONSTANTS 1
#define CALIBRATION_CONSTANTS 1
#define USE_STOP1 1
#define STOP1_WAKE_EXTI_GROUP1_MASK (1U << 0)
#define CONFIG_HAS_HIBERNATE 0
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

#endif
