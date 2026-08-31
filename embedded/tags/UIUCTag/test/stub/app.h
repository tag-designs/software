/* stub: the pieces of the runtime umbrella header state_run.c relies on. */
#ifndef STUB_APP_H
#define STUB_APP_H

/* Mirrors CASSERT from embedded/tags/common/core/inc/core_types.h, so the
   simulation checks the same compile-time invariants the firmware does. */
#define CASSERT(predicate) _impl_CASSERT_LINE(predicate, __LINE__)
#define _impl_PASTE(a, b) a##b
#define _impl_CASSERT_LINE(predicate, line) \
  typedef char _impl_PASTE(assertion_failed_, line)[2 * !!(predicate)-1];

#endif
