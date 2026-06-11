/**
 * @file custom.h
 * @brief CompassTag AT25 breakout variant build constants.
 * @author tag firmware authors
 * @date 2026-05-23
 */

#ifndef CUSTOM_H
#define CUSTOM_H

/** @name Firmware identity
 * Strings, protocol sizing, and board compatibility switches.
 * @{
 */
#define FIRMWARE_STRING "CompassTagv1, Firmware version 1"
#undef  BOARD_NAME
#define BOARD_NAME "CompassTagv1"
#define QTMONITOR_VERSION 2.0
#define PROTOBUFSIZE 4096
#define TAG_STORAGE_SPI_DMA_BLOCK_READ 1
#define TAG_STORAGE_SPI_DMA_BLOCK_WRITE 1

#define SWAP_I2C 1
/** @} */

/** @name Sensor conversion constants
 * Board-level scale constants used when converting raw log samples for host
 * monitor ACK payloads.
 * @{
 */
#define ACCEL_CONSTANT 0.976f
#define MAG_CONSTANT 0.01
/** @} */

#endif
