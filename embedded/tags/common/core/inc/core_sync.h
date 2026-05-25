/**
 * @file core_sync.h
 * @brief Shared synchronization objects and runtime timestamps.
 * @author tag firmware authors
 * @date 2026-05-23
 */

#ifndef TAG_CORE_SYNC_H
#define TAG_CORE_SYNC_H

#include <stdint.h>

#include "ch.h"

extern binary_semaphore_t SPI1mutex;
extern binary_semaphore_t USART2mutex;
extern int32_t timestamp;
extern uint32_t timestamp_millis;
extern thread_t *tpMain;

#endif
