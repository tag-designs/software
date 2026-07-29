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
/** @brief Main ChibiOS thread handle used by interrupt/event producers. */
extern thread_t *tpMain;

#endif
