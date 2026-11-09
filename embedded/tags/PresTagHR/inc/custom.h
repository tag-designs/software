/**
 * @file custom.h
 * @brief PresTagHR variant build constants.
 *
 * @details Matches PresTag in every sleep mode and timing except the LPS27
 *          readiness wait, which allows for the longer low-noise conversion.
 */

#ifndef CUSTOM_H
#define CUSTOM_H

/** @name Firmware identity
 * Strings and protocol sizing reported to host tools.
 * @{
 */
#define FIRMWARE_STRING "PresTagHRv1, Firmware version 1"
#undef  BOARD_NAME
#define BOARD_NAME "PresTagHRv1"
#define QTMONITOR_VERSION 2.0
#define PROTOBUFSIZE 4096
/** @} */

#define TAG_IDLE_SLEEP_MODE SHUTDOWN
#define TAG_CONFIGURED_SLEEP_MODE SHUTDOWN
#define TAG_HIBERNATING_SLEEP_MODE SHUTDOWN
#define TAG_FINISHED_SLEEP_MODE SHUTDOWN
#define TAG_ABORTED_SLEEP_MODE SHUTDOWN

/* LPS_LOW_POWER governs only lps27GetPressureTemp(), which PresTagHR does not
 * sample with (the self-test still does); the run's noise mode comes from the
 * stored configuration through lps27GetPressureTempFull(). */
#define LPS_LOW_POWER 1
#define LPS27_POWERUP_MS 5U
#define LPS27_READY_POLL_MS 5U
/* Low-noise one-shot conversions take longer than low-power ones. The poll
 * loop leaves on the first ready status, so extra polls cost nothing when the
 * data is ready at the first; set from the bench measurement of both modes. */
#define LPS27_READY_POLLS 6
#define PRESTAG_RUNNING_LONG_SLEEP_MODE SHUTDOWN

/* RTC Alarm A as the stop-delay tick source; see PresTag's custom.h. */
#define TAG_STOP_RTC_TICKER 1

#endif
