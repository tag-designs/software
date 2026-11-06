---
type: worklist
status: current
summary: Open work on the base and programmer firmware -- SWD fault diagnostics for field bases, unbuilt SPI SWD sources, and SWD speed ideas for a future base.
---

# Bases Worklist

Delete an item when it is done.

- **One failed read can fail the next several block reads.** With the parity
  bug of [2026-11](design/investigations/2026-11-breakout-parity-bit.md) on
  `tag-breakout-base-l432-v1`, 112 single parity errors left 25 block reads
  failed twice, in two bursts of 5 and 9 within a few milliseconds, each
  ended only by a host retry. So the base's recovery after a failed `DRW`
  read (`errorClear()`, the abort, the dummy `RDBUFF` read) does not always
  return the AP to a usable state. Find out why with the serial log, by
  injecting a fault; any real link error would cascade the same way.
- **SWD fault diagnostics on field bases.** `bittag-base-v7` and
  `tag-breakout-base-l432-v1` log SWD faults on a USB serial port (branch
  `base-0x81`), which found the 0x81 cause
  (`design/investigations/2026-10-0x81-dbg-sleep.md`). Field bases cannot be
  reflashed with that; a vendor command beside `STLINK_TAGBASE_IDENTIFY`
  (F6 99) could return a ring buffer of the last faults (acknowledgement,
  register or address, retries) for any host tool to dump after a run.
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
