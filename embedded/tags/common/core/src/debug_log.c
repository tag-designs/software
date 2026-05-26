/**
 * @file debug_log.c
 * @brief Monitor-readable debug message queue implementation.
 * @author tag firmware authors
 * @date 2026-05-23
 */

#include "debug_log.h"

#if defined(TAG_DEBUG_LOG) && TAG_DEBUG_LOG

/** @name Debug log queue
 * Backing store for the monitor debug-message stream.
 *
 * Treat this as a queue of diagnostic strings. monitor.c drains at most one
 * Ack.status.debug_message payload at a time, but the backing queue is larger
 * so several messages can accumulate between host polls.
 * @{
 */
#include "core_types.h"

#include <stdarg.h>

#define DEBUG_LOG_BUFFER_SIZE 1024

static MemoryStream debug_stream;
static uint8_t debug_message_buffer[DEBUG_LOG_BUFFER_SIZE] NOINIT;
static bool debug_log_initialized;

/**
 * @brief Initialize the in-memory debug-message queue.
 */
void debug_log_init(void)
{
  msObjectInit(&debug_stream, debug_message_buffer, sizeof(debug_message_buffer), 0);
  debug_log_initialized = true;
}

/**
 * @brief Report whether the monitor can drain at least one debug byte.
 *
 * @return true when queued debug text is available.
 */
bool debug_log_available(void)
{
  if (!debug_log_initialized)
  {
    return false;
  }
  return debug_stream.eos != debug_stream.offset;
}

/**
 * @brief Drain queued debug bytes for transport to the host monitor.
 *
 * @param[out] buf Destination buffer for queued bytes.
 * @param[in] len Maximum number of bytes to copy.
 * @return Number of bytes copied into buf.
 */
size_t debug_log_read(uint8_t *buf, size_t len)
{
  if (!debug_log_initialized)
  {
    return 0;
  }
  return streamRead(&debug_stream, buf, len);
}

/**
 * @brief Return a stream that drivers can write diagnostic text into.
 *
 * @return Sequential stream backed by the debug queue, or NULL if unavailable.
 */
BaseSequentialStream *debug_log_stream(void)
{
  if (!debug_log_initialized)
  {
    return NULL;
  }
  return (BaseSequentialStream *)&debug_stream;
}

/**
 * @brief Append formatted diagnostic text to the monitor-readable queue.
 *
 * @param[in] fmt printf-style format string.
 * @return Number of bytes written to the queue.
 */
int debug_log_printf(const char *fmt, ...)
{
  if (!debug_log_initialized)
  {
    return 0;
  }

  va_list ap;
  va_start(ap, fmt);
  int written = chvprintf((BaseSequentialStream *)&debug_stream, fmt, ap);
  va_end(ap);
  return written;
}
/** @} */

#endif
