/**
 * @file core_sync.h
 * @brief Shared synchronization objects and runtime timestamps.
 * @author tag firmware authors
 * @date 2026-05-23
 */

#ifndef TAG_CORE_SYNC_H
#define TAG_CORE_SYNC_H

#include <stdbool.h>
#include <stdint.h>

#include "ch.h"

/** @brief Shared lock for SPI1 bus sessions. */
extern binary_semaphore_t SPI1mutex;
/** @brief Shared lock for USART2 synchronous-bus sessions. */
extern binary_semaphore_t USART2mutex;
/** @brief Current RTC time in Unix seconds, maintained by the main loop. */
extern int32_t timestamp;
/** @brief Millisecond remainder paired with timestamp. */
extern uint32_t timestamp_millis;
/** @brief Whether the STM32 RTC calendar was initialized at boot. */
extern bool rtcInitializedAtBoot;
/**
 * @brief Whether boot established a wall clock worth scheduling against.
 *
 * @details False when the backup domain was lost and the external RTC could not
 *          supply a replacement time. State transitions that commit the tag to
 *          collecting data must be gated on this: an untrusted clock makes the
 *          configured start and stop comparisons meaningless, which strands the
 *          tag in RUNNING at collection current.
 *
 * @note Set during boot reset recovery and again by any successful
 *       SetTimeUnixSec(), so a host clock synchronization makes the clock
 *       trustworthy immediately. Gating a later decision on a boot-only snapshot
 *       was wrong: it delayed every configured start to the next minute alarm
 *       even though the host had just set the clock.
 */
extern bool clockTrusted;
/** @brief Main ChibiOS thread handle used by interrupt/event producers. */
extern thread_t *tpMain;

#endif
