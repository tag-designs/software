---
type: worklist
status: current
summary: Open work on the base and programmer firmware: the 0x81 (STLINK_DEBUG_ERR_FAULT) errors while a tag is busy, unbuilt SPI SWD sources, and SWD speed ideas for a future base.
---

# Bases Worklist

Delete an item when it is done.

- **0x81 errors while the tag is busy.** Host tools see the base answer
  `STLINK_DEBUG_ERR_FAULT` (0x81) in bursts while a tag is erasing flash,
  setting its RTC or running self-tests: `read debug register 0xe000edfc failed
  status=0x81` and `RPC buffer write failed` in tagcore's log. Calls recover on
  retry, but a burst can hide the end of an erase from qtmonitor (seen
  2026-10-27 on a UIUCTag breakout, `tag-breakout-base-l432-v1`), and Tag
  Programmer and `qtmon_session_check` show the same bursts.
  - **Lead:** `SWD_Transaction()` in `common/src/ll_swd.c` retries a WAIT
    acknowledgement `MAX_SWD_RETRY` (25, `common/inc/dp_swd.h`) times with no
    delay, and the debug-register paths poll `REGRETRIES` (20) times. A target
    stalling its bus for a flash or RTC operation may outlast both. Try a
    retry bounded by time (or with backoff) before anything else.
  - **Seen so far (2026-10-27):** Synchronize Clock and Self-Test each set
    off a burst. The failing accesses are SWD writes into the tag's SRAM
    (the RPC buffer) and debug-register reads, in the second or so after
    the command is accepted, while the tag sets its RTC or runs its tests.
    The tag's stop-mode delay (`stopMilliseconds()` in
    `embedded/tags/common/core/src/time.c`, which zeroes `DBGMCU->CR`) is
    not the cause: both versions use an ordinary RTOS sleep while a
    monitor session is open. A busy CPU cannot fail an SWD access by
    itself, so suspect the bus or clocks being disturbed (RTC and
    backup-domain writes, sensor and clock setup in the tests) for longer
    than the retries last. qtmonitor now pauses its polls for 1.5 s after
    these two commands, which quietens the log but does not fix the cause.
  - **Debug connector:** a separate SWD connector to the base MCU is planned,
    to break on the fault path and see the acknowledgement the target
    returned.
  - **Diagnostic command:** a vendor command beside `STLINK_TAGBASE_IDENTIFY`
    (F6 99) could return a ring buffer of the last SWD faults (acknowledgement,
    register or address, retry count) and running totals. Any host tool could
    dump it after a run, and it works on field bases where no probe can be
    attached.

- **`tag-breakout-base-l432-v1/src/ll_swd_spi.c` is not built.** The target
  compiles `ll_swd.c`. A note in that directory recorded that `ll_swd_spi.c`
  did not work properly on macOS with ChibiOS 21.11. Fix it or remove it, and
  remove `src/ll_swd_spi.c-back` beside it.
- **Unused local sources.** `tag-breakout-base-l432-u375-1v8/src/ll_swd.c` and
  `tag-base-c071/src/stlink-local.c` and `src/ll_swd.c` are not in their
  targets' `project.mk`.
- **Hybrid SPI SWD for a faster F042 base.** The F042 bases still bitbang
  everything. In the optimized `tag-breakout-base-l432-v1/src/ll_swd.c`, every
  shift-out width is a multiple of 8 bits (8, 16, 24, 32) and the odd widths
  are all shift-in (5, 9, 24), so SPI can carry every shift-out phase. That is
  the split `tag-base-c071` and `tag-breakout-base-l432-u375-1v8` already use.
  It was considered for a future base rather than a retrofit.
- **Hot SWD helpers in RAM.** On a future base with a faster MCU and SRAM
  headroom, consider placing only the hottest bitbang helpers in RAM. The F042
  base is not a candidate: its build uses essentially all of its SRAM.
