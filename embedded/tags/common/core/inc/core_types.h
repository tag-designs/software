/**
 * @file core_types.h
 * @brief Common low-level macros and sleep-mode types for tag firmware.
 * @author tag firmware authors
 * @date 2026-05-23
 */

#ifndef TAG_CORE_TYPES_H
#define TAG_CORE_TYPES_H

//#include "ch.h"
//#include "hal.h"

/**
 * @def CASSERT
 * @brief Emit a compile-time assertion in C code that predates static_assert.
 *
 * @param[in] predicate Constant expression that must evaluate true.
 */
// Compile-time checks.
#define CASSERT(predicate) _impl_CASSERT_LINE(predicate, __LINE__, __FILE__)
#define _impl_PASTE(a, b) a##b
#define _impl_CASSERT_LINE(predicate, line, file)      \
  typedef char _impl_PASTE(assertion_failed_##file##_, \
                           line)[2 * !!(predicate)-1];

/**
 * @def NOINIT
 * @brief Place retained scratch storage in the linker-selected nonzeroed RAM.
 */
// Attribute for uninitialized data that should not live in bss or data.
#define NOINIT __attribute__((section(".ram0")))

/*
 * Detect the debug-reset/vector-catch hint used during monitor attach reset
 * recovery. Runtime code that needs to know whether the monitor session is
 * currently attached should call monitorIsAttached() instead.
 */
#define MONCONNECTED (CoreDebug->DEMCR & CoreDebug_DEMCR_VC_CORERESET_Msk)

/**
 * @brief Report whether a monitor session is currently attached.
 *
 * @return true when the firmware believes the host monitor is attached.
 */
extern bool monitorIsAttached(void);

/**
 * @brief Report whether monitor support is enabled for this build/runtime.
 *
 * @return true when monitor request handling is available.
 */
extern bool isMonitorEnabled(void);

/**
 * @enum Sleep
 * @brief Low-power or idle mode requested by the state machine.
 */
enum Sleep {
  SHUTDOWN, ///< Enter STM32 shutdown mode.
  STANDBY,  ///< Enter STM32 standby mode.
  STOP1,    ///< Enter STM32 Stop1 mode.
  STOP2,    ///< Enter STM32 Stop2 mode.
  SLEEP,    ///< Enter ordinary ARM sleep.
  STOP0     ///< Enter STM32 Stop0 mode.
};

#endif
