#include "debug_log.h"

#ifdef TAG_DEBUG_LOG

/*
 * Backing store for the monitor debug-message stream.
 *
 * The wire protocol returns one Ack.debug_message payload per Req.debug
 * request, so the stream buffer is sized to exactly one payload. Writers append
 * with debug_log_printf(); monitor.c drains the buffer through debug_log_read().
 */
#include "core_types.h"
#include "tag.pb.h"

#include <stdarg.h>

static MemoryStream debug_stream;
static uint8_t debug_message_buffer[sizeof(((Ack *)0)->payload.debug_message)] NOINIT;
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
