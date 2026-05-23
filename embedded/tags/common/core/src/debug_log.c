#include "debug_log.h"

#ifdef TAG_DEBUG_LOG

/*
 * Backing store for the monitor debug-message stream.
 *
 * Treat this as a queue of diagnostic strings. monitor.c drains at most one
 * Ack.status.debug_message payload at a time, but the backing queue is larger
 * so several messages can accumulate between host polls.
 */
#include "core_types.h"

#include <stdarg.h>

#define DEBUG_LOG_BUFFER_SIZE 1024

static MemoryStream debug_stream;
static uint8_t debug_message_buffer[DEBUG_LOG_BUFFER_SIZE] NOINIT;
static bool debug_log_initialized;

void debug_log_init(void)
{
  msObjectInit(&debug_stream, debug_message_buffer, sizeof(debug_message_buffer), 0);
  debug_log_initialized = true;
}

bool debug_log_available(void)
{
  if (!debug_log_initialized)
  {
    return false;
  }
  return debug_stream.eos != debug_stream.offset;
}

size_t debug_log_read(uint8_t *buf, size_t len)
{
  if (!debug_log_initialized)
  {
    return 0;
  }
  return streamRead(&debug_stream, buf, len);
}

BaseSequentialStream *debug_log_stream(void)
{
  if (!debug_log_initialized)
  {
    return NULL;
  }
  return (BaseSequentialStream *)&debug_stream;
}

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

#endif
