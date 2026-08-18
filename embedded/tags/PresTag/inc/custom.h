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

#define TAG_SHUTDOWN_ENTERS_STANDBY 0
#define TAG_IDLE_SLEEP_MODE SHUTDOWN
#define TAG_CONFIGURED_SLEEP_MODE SHUTDOWN
#define TAG_HIBERNATING_SLEEP_MODE SHUTDOWN
#define TAG_FINISHED_SLEEP_MODE SHUTDOWN
#define TAG_ABORTED_SLEEP_MODE SHUTDOWN

#define LPS_LOW_POWER 1

#endif
