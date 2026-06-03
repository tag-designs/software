/**
 * @file core_types.h
 * @brief Common low-level macros and sleep-mode types for tag firmware.
 * @author tag firmware authors
 * @date 2026-05-23
 */

#ifndef TAG_CORE_TYPES_H
#define TAG_CORE_TYPES_H

#include "ch.h"
#include "hal.h"

// Compile-time checks.
#define CASSERT(predicate) _impl_CASSERT_LINE(predicate, __LINE__, __FILE__)
#define _impl_PASTE(a, b) a##b
#define _impl_CASSERT_LINE(predicate, line, file)      \
  typedef char _impl_PASTE(assertion_failed_##file##_, \
                           line)[2 * !!(predicate)-1];

// Attribute for uninitialized data that should not live in bss or data.
#define NOINIT __attribute__((section(".ram0")))

// Detect connected monitor/debugger.
#define MONCONNECTED (CoreDebug->DEMCR & CoreDebug_DEMCR_VC_CORERESET_Msk)

enum Sleep { SHUTDOWN, STANDBY, STOP1, STOP2, SLEEP };

#endif
