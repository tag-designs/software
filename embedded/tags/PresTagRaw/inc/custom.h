/**
 * @file custom.h
 * @brief PresTagRaw variant build constants.
 * @author tag firmware authors
 * @date 2026-05-23
 */

#ifndef CUSTOM_H
#define CUSTOM_H

/** @name Firmware identity
 * Strings and protocol sizing reported to host tools.
 * @{
 */
#define FIRMWARE_STRING "PresTagRawv1, Firmware version 1"
#undef  BOARD_NAME
#define BOARD_NAME "PresTagRawv1"
#define QTMONITOR_VERSION 2.0
#define PROTOBUFSIZE 4096
/** @} */

#define LPS_LOW_POWER 1

/* RTC Alarm A as the stop-delay tick source. The board LSE is 1024 Hz, which
 * makes per-delay LPTIM re-arming cost more than the delay itself; Alarm A has
 * no synchronisation wait on the hot path. See TAG_STOP_RTC_TICKER in
 * common/core/src/time.c. */
#define TAG_STOP_RTC_TICKER 1

#endif
