/**
 * @file debug_log.c
 * @brief Monitor-readable debug message queue implementation.
 * @author tag firmware authors
 * @date 2026-05-23
 */

#include "debug_log.h"

#if defined(TAG_DEBUG_LOG) && TAG_DEBUG_LOG

#include "ch.h"

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
#include <string.h>

#define DEBUG_LOG_BUFFER_SIZE 1024

static MemoryStream debug_stream;
static uint8_t debug_message_buffer[DEBUG_LOG_BUFFER_SIZE] NOINIT;
static binary_semaphore_t debug_log_sem;
static bool debug_log_initialized;

static void debug_log_compact_locked(void)
{
  if (debug_stream.offset == 0U)
  {
    return;
  }

  if (debug_stream.offset == debug_stream.eos)
  {
    debug_stream.offset = 0U;
    debug_stream.eos = 0U;
    return;
  }

  const size_t unread = debug_stream.eos - debug_stream.offset;
  memmove(debug_stream.buffer, debug_stream.buffer + debug_stream.offset, unread);
  debug_stream.offset = 0U;
  debug_stream.eos = unread;
}

/**
 * @brief Initialize the in-memory debug-message queue.
 */
void debug_log_init(void)
{
  chBSemObjectInit(&debug_log_sem, false);
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
  bool available;

  if (!debug_log_initialized)
  {
    return false;
  }

  chBSemWait(&debug_log_sem);
  available = debug_stream.eos != debug_stream.offset;
  chBSemSignal(&debug_log_sem);
  return available;
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
  size_t bytes;

  if (!debug_log_initialized)
  {
    return 0;
  }

  chBSemWait(&debug_log_sem);
  bytes = streamRead(&debug_stream, buf, len);
  debug_log_compact_locked();
  chBSemSignal(&debug_log_sem);
  return bytes;
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
  int written;

  if (!debug_log_initialized)
  {
    return 0;
  }

  va_list ap;
  va_start(ap, fmt);
  chBSemWait(&debug_log_sem);
  debug_log_compact_locked();
  written = chvprintf((BaseSequentialStream *)&debug_stream, fmt, ap);
  chBSemSignal(&debug_log_sem);
  va_end(ap);
  return written;
}
/** @} */

#endif
