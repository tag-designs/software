/**
 * @file core_events.h
 * @brief Shared event-mask definitions for sensors, RTC, wakeup, and monitor commands.
 * @author tag firmware authors
 * @date 2026-05-23
 */

#ifndef TAG_CORE_EVENTS_H
#define TAG_CORE_EVENTS_H

#include "ch.h"

extern eventmask_t events;

// Event/work bit assignments.
#define EVT_BIT_WKUP 7
#define EVT_BIT_RTC_ALRAF 8
#define EVT_BIT_RTC_ALRBF 9
#define EVT_BIT_RTC_WUTF 10
#define EVT_BIT_WAKE_STANDBY 11
#define EVT_BIT_MON_WORK_START 12
#define EVT_BIT_MON_WORK_STOP 13
#define EVT_BIT_MON_WORK_RESET 14
#define EVT_BIT_MON_WORK_SELFTEST 15
#define EVT_BIT_MON_WORK_CALIBRATE 16
#define EVT_BIT_MONITOR_SERVICE 17
#define EVT_BIT_MONITOR_TIMEOUT 18

// Generic wakeup event.
#define EVT_WKUP EVENT_MASK(EVT_BIT_WKUP)

// STM32 RTC event bits. These match the hardware status bit numbers used here.
#define EVT_RTC_ALRAF EVENT_MASK(EVT_BIT_RTC_ALRAF)
#define EVT_RTC_ALRBF EVENT_MASK(EVT_BIT_RTC_ALRBF)
#define EVT_RTC_WUTF EVENT_MASK(EVT_BIT_RTC_WUTF)
#define EVT_RTC_MASK(x) ((x) & (0x7 << 8))
#define EVT_RTC_ALL (EVT_RTC_ALRAF | EVT_RTC_ALRBF | EVT_RTC_WUTF)

// Standby/shutdown wakeup.
#define EVT_WAKE_STANDBY EVENT_MASK(EVT_BIT_WAKE_STANDBY)

// Monitor command work bits. These are returned by protobuf evaluation and
// consumed by the state machine; they are not signaled as ChibiOS events.
#define MON_WORK_START EVENT_MASK(EVT_BIT_MON_WORK_START)
#define MON_WORK_STOP EVENT_MASK(EVT_BIT_MON_WORK_STOP)
#define MON_WORK_RESET EVENT_MASK(EVT_BIT_MON_WORK_RESET)
#define MON_WORK_SELFTEST EVENT_MASK(EVT_BIT_MON_WORK_SELFTEST)
#define MON_WORK_CALIBRATE EVENT_MASK(EVT_BIT_MON_WORK_CALIBRATE)

// Actual monitor events signaled into the main thread.
#define EVT_MONITOR_SERVICE EVENT_MASK(EVT_BIT_MONITOR_SERVICE)
#define EVT_MONITOR_TIMEOUT EVENT_MASK(EVT_BIT_MONITOR_TIMEOUT)
#define EVT_MONITOR_ALL (EVT_MONITOR_SERVICE | EVT_MONITOR_TIMEOUT)
#define MON_WORK_ALL (MON_WORK_START | MON_WORK_STOP | MON_WORK_RESET | MON_WORK_SELFTEST | MON_WORK_CALIBRATE)

#define EVT_HARDWARE_ALL (EVT_WKUP | EVT_RTC_ALL | EVT_WAKE_STANDBY)
#define EVT_ALL_DEFINED (EVT_HARDWARE_ALL | MON_WORK_ALL | EVT_MONITOR_ALL)

#if defined(__cplusplus)
static_assert((EVT_HARDWARE_ALL & MON_WORK_ALL) == 0, "hardware events overlap monitor work bits");
static_assert((EVT_HARDWARE_ALL & EVT_MONITOR_ALL) == 0, "hardware events overlap monitor event bits");
static_assert((MON_WORK_ALL & EVT_MONITOR_ALL) == 0, "monitor work bits overlap monitor event bits");
#else
_Static_assert((EVT_HARDWARE_ALL & MON_WORK_ALL) == 0, "hardware events overlap monitor work bits");
_Static_assert((EVT_HARDWARE_ALL & EVT_MONITOR_ALL) == 0, "hardware events overlap monitor event bits");
_Static_assert((MON_WORK_ALL & EVT_MONITOR_ALL) == 0, "monitor work bits overlap monitor event bits");
#endif

#endif
