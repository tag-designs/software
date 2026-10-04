---
type: worklist
status: open
summary: Tracker of known tag firmware defects, separating reproduced faults from ones found by reading code, with resolved entries kept.
---

# Open Issues — Tag Firmware

Known defects that are understood well enough to write down but are not fixed.
Each entry says what the evidence actually is, so the next person can tell a
reproduced fault from one found by reading code.

Last reviewed 2026-10-01.

## Reproduced

History (flash error-flag clear, Standby arming window, SRAM2 page 3 retention): see [2026-09-u375-standby-layout-dependence](../common/core/design/investigations/2026-09-u375-standby-layout-dependence.md).

History (run-mode Stop 1 current is layout-dependent; resolved by Stop 2): see [0007-u375-run-sleep-is-stop-2](../../../docs/decisions/0007-u375-run-sleep-is-stop-2.md).

### Standby request declined by layout: resolved by Stop 3

History: see [0008-u375-terminal-sleep-is-stop-3](../../../docs/decisions/0008-u375-terminal-sleep-is-stop-3.md) and [2026-09-u375-standby-layout-dependence](../common/core/design/investigations/2026-09-u375-standby-layout-dependence.md).

History (write-error page skip, reverted then re-landed under Stop 3): see [2026-09-u375-standby-layout-dependence](../common/core/design/investigations/2026-09-u375-standby-layout-dependence.md).

History (non-monotonic timestamps, intermittent download failure, STATE_UNSPECIFIED after reset, unserviced stop request): see [2026-09-attach-storm-failures](investigations/2026-09-attach-storm-failures.md).

## Found by reading code, not reproduced

None of these is known to cause a current symptom, except where an entry says
otherwise. Fixes for the first three, and for the marker-log entry, are
scheduled in [Next Release TODO](next-release-todo.md).

- **CompassTag family: resumed logging overwrites earlier pages.** This one
  loses data. `Running(T_INIT)` (`state_run.c:62`) and `restoreLog()`
  (`datalog.c:231`) reset the external cursor to `pages * 30`. That is a
  sample count, but the cursor is in 16-bit words, and a page is 190 of them.
  So any resume after hibernation or a restart writes over earlier pages. It
  affects the shipping CompassTagAT25. TODO A1.
- **CompassTag family: `t_DataHeader.temp10` is `uint16_t`** but holds a signed
  value, so sub-zero core temperatures read as about 6553 °C. TODO A2.
- **PresTag, CompassTag, UIUCTag: the last partial page is never downloaded.**
  Writes run to the end of flash, but `data_logAck()` serves only whole pages.
  TODO A3.
- **The state-marker log stops silently when full**, which leaves no trace in a
  field build. TODO A4.

- **`gd5fSectorErase()` reports success without erasing** on three paths:
  logical block out of range, mapping failure, and physical block out of range.
  A caller cannot distinguish "erased" from "silently skipped".
- **`gd5fRead()` does not invalidate `gd5f_cache_active`.** The flag is cleared
  only in `gd5fProgramCacheLoad()` and `gd5fProgramExecuteCache()`, so a read
  loads a different page into the device cache register while the flag still
  claims the programmed page is resident.
- **Unbounded hardware waits** in the IMUTagNandBmp581 RTC LLD:
  `hal_rtc_lld.c` lines 631 (`ALRAWF`), 648 (`ALRBWF`), 727 (`WUTWF`), and the
  `do`/`while` at 576 inside a critical zone. Any of them hangs the tag if the
  bit never sets.
- **Asymmetric wakeup-timer disable.** `rtcSTM32SetPeriodicWakeup()` waits for
  `WUTWF` when arming but not when disarming. Investigated and **exonerated**
  as a cause of the idle fault -- compiling the wakeup timer out entirely did
  not change it -- but the asymmetry is still there. Note that the IMUTag
  family never calls `enableTicker()`, so this target never arms the timer.

## Agreed work, not started

- ~~Rework `eraseExternal()` liveness.~~ Done, in two parts. The model --
  while no event, erase; if an event is pending, return -- is what `Reset()`
  in `state_machine.c` implements around the incremental
  `eraseExternalNextSector()`, testing the pending-event mask after every
  sector. The blocking `eraseExternal()` in the IMUTag `datalog.c` is dead on
  this target; its `chThdYield()` was removed on 2026-09-07 and the function
  documented as the synchronous sweep it is. The shipped image is byte-identical
  before and after, so no measurement was owed.
- Consider a full erase sweep when a run did not finish with clearly
  recoverable boundaries.
