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

// ADXL event bits.
#define EVT_ADXL_DATA_READY EVENT_MASK(0)
#define EVT_ADXL_FIFO_READY EVENT_MASK(1)
#define EVT_ADXL_FIFO_WATERMARK EVENT_MASK(2)
#define EVT_ADXL_FIFO_OVERRUN EVENT_MASK(3)
#define EVT_ADXL_ACT EVENT_MASK(4)
#define EVT_ADXL_INACT EVENT_MASK(5)
#define EVT_ADXL_AWAKE EVENT_MASK(6)
#define EVT_ADXL_MASK(x) ((x)&0x7f)

// Generic wakeup event.
#define EVT_WKUP EVENT_MASK(7)

// STM32 RTC event bits. These match the hardware status bit numbers used here.
#define EVT_RTC_ALRAF EVENT_MASK(8)
#define EVT_RTC_ALRBF EVENT_MASK(9)
#define EVT_RTC_WUTF EVENT_MASK(10)
#define EVT_RTC_MASK(x) ((x) & (0x7 << 8))

// Standby/shutdown wakeup.
#define EVT_WAKE_STANDBY EVENT_MASK(11)

// Monitor command events.
#define EVT_START EVENT_MASK(12)
#define EVT_STOP EVENT_MASK(13)
#define EVT_RESET EVENT_MASK(14)
#define EVT_SELFTEST EVENT_MASK(15)
#define EVT_CALIBRATE EVENT_MASK(16)
#define EVT_MONITOR_ALL (EVT_START | EVT_STOP | EVT_RESET | EVT_SELFTEST | EVT_CALIBRATE)

#endif
