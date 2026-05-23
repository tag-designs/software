/**
 * @file custom.h
 * @brief BitPresTag variant build constants and board aliases.
 * @author tag firmware authors
 * @date 2026-05-23
 */

#ifndef CUSTOM_H
#define CUSTOM_H

/** @name Firmware identity
 * Strings and protocol sizing reported to host tools.
 * @{
 */
#define FIRMWARE_STRING "BitPresTagv4, Firmware version 1"
#undef  BOARD_NAME
#define BOARD_NAME "BitPresTagv1"
#define SWAP_I2C TRUE
#define QTMONITOR_VERSION 2.0
#define PROTOBUFSIZE 4096
/** @} */

/** @name Board signal aliases
 * Compatibility names consumed by shared family code and legacy drivers.
 * @{
 */
#define LINE_STEVAL_CS LINE_LPS_CS
#define LINE_ACCEL_INT LINE_WKUP1
/** @} */

/** @name Fast-clock policy
 * MSI and flash wait-state settings used while servicing monitor log reads.
 * @{
 */
#define STM32_MSIRANGE_FAST STM32_MSIRANGE_24M
#define RANGE_MULTIPLIER 12
#define FLASH_WS_SLOW 0
#define FLASH_WS_FAST 3
/** @} */

#endif
