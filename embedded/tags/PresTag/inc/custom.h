/**
 * @file custom.h
 * @brief PresTag variant build constants.
 * @author tag firmware authors
 * @date 2026-05-23
 */

#ifndef CUSTOM_H
#define CUSTOM_H

/** @name Firmware identity
 * Strings and protocol sizing reported to host tools.
 * @{
 */
#define FIRMWARE_STRING "PresTagv4, Firmware version 1"
#undef  BOARD_NAME
#define BOARD_NAME "PresTagv3"
#define QTMONITOR_VERSION 2.0
#define PROTOBUFSIZE 4096
/** @} */

#define LPS_LOW_POWER 1

#endif
