/**
 * @file debug_log.h
 * @brief Optional monitor-readable debug message queue.
 * @author tag firmware authors
 * @date 2026-05-23
 */

#ifndef TAG_CORE_DEBUG_LOG_H
#define TAG_CORE_DEBUG_LOG_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** @name Debug log API
 * Optional monitor-readable debug log.
 *
 * Tags enable this by selecting the debug_log build module, which defines
 * TAG_DEBUG_LOG and compiles debug_log.c. When the module is absent, the public
 * API becomes a set of no-op helpers so drivers can keep diagnostic calls in
 * place without carrying local #ifdef blocks.
 * @{
 */
#if defined(TAG_DEBUG_LOG) && TAG_DEBUG_LOG
#include "chprintf.h"
#include "memstreams.h"

/**
 * @brief Initialize the in-memory debug-message queue.
 */
void debug_log_init(void);

/**
 * @brief Report whether the monitor can drain at least one debug byte.
 *
 * @return true when queued debug text is available.
 */
bool debug_log_available(void);

/**
 * @brief Drain queued debug bytes for transport to the host monitor.
 *
 * @param[out] buf Destination buffer for queued bytes.
 * @param[in] len Maximum number of bytes to copy.
 * @return Number of bytes copied into buf.
 */
size_t debug_log_read(uint8_t *buf, size_t len);

/**
 * @brief Return a stream that drivers can write diagnostic text into.
 *
 * @return Sequential stream backed by the debug queue, or NULL if unavailable.
 */
BaseSequentialStream *debug_log_stream(void);

/**
 * @brief Append formatted diagnostic text to the monitor-readable queue.
 *
 * @param[in] fmt printf-style format string.
 * @return Number of bytes written to the queue.
 */
int debug_log_printf(const char *fmt, ...);

#else

/**
 * @brief No-op debug-log initializer used when the module is absent.
 */
static inline void debug_log_init(void)
{
}

/**
 * @brief Report that no debug bytes are available when the module is absent.
 *
 * @return false because no queue exists in this build.
 */
static inline bool debug_log_available(void)
{
  return false;
}

/**
 * @brief No-op debug-log read used when the module is absent.
 *
 * @param[out] buf Ignored destination buffer.
 * @param[in] len Ignored maximum length.
 * @return 0 because no queue exists in this build.
 */
static inline size_t debug_log_read(uint8_t *buf, size_t len)
{
  (void)buf;
  (void)len;
  return 0;
}

/**
 * @brief No-op formatted diagnostic write used when the module is absent.
 *
 * @param[in] fmt Ignored printf-style format string.
 * @return 0 because no queue exists in this build.
 */
static inline int debug_log_printf(const char *fmt, ...)
{
  (void)fmt;
  return 0;
}
#endif
/** @} */

#endif
