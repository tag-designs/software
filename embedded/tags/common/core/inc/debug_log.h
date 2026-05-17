#ifndef TAG_CORE_DEBUG_LOG_H
#define TAG_CORE_DEBUG_LOG_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Optional monitor-readable debug log.
 *
 * Tags enable this by selecting the debug_log build module, which defines
 * TAG_DEBUG_LOG and compiles debug_log.c. When the module is absent, the public
 * API becomes a set of no-op helpers so drivers can keep diagnostic calls in
 * place without carrying local #ifdef blocks.
 */
#ifdef TAG_DEBUG_LOG
#include "chprintf.h"
#include "memstreams.h"

void debug_log_init(void);
bool debug_log_available(void);
size_t debug_log_read(uint8_t *buf, size_t len);
BaseSequentialStream *debug_log_stream(void);
int debug_log_printf(const char *fmt, ...);

#else

static inline void debug_log_init(void)
{
}

static inline bool debug_log_available(void)
{
  return false;
}

static inline size_t debug_log_read(uint8_t *buf, size_t len)
{
  (void)buf;
  (void)len;
  return 0;
}

static inline int debug_log_printf(const char *fmt, ...)
{
  (void)fmt;
  return 0;
}
#endif

#endif
