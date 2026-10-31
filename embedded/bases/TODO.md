---
type: worklist
status: current
summary: Open work on the base and programmer firmware: SWD fault diagnostics for field bases, unbuilt SPI SWD sources, and SWD speed ideas for a future base.
---

# Bases Worklist

Delete an item when it is done.

- **Retry the SWD connect in `SWD_Open()`.** A tag asleep in Shutdown has
  no debug port until nRST wakes it (about 250 us on the L432), and the host's
  SWD enter, sent about a millisecond after it asserts nRST, sometimes arrives
  first: `SWD_Connect()` gets no answer, `SWD_Open()` fails, and since a plain
  debug-register read never reopens the port, every read of the session
  fails with 0x81. The tagcore libraries now retry the enter
  (`host/libraries/tagcore/design/investigations/2026-10-l4-attach-swd-enter.md`),
  but probe-rs's own ST-LINK driver, which Tag Programmer and `tag-flash` use
  on bases with the ST-LINK fixes, and any other host software, do not. Retry
  the first connect a few times about 1 ms apart inside `SWD_Open()`, so it
  costs nothing when the port answers at once and covers any host and any
  order of commands. Apply it to every `ll_swd.c` a target builds (the
  common one and the local copies that replace it), then reflash the bench
  bases and check with an attach loop on a BitTag in Shutdown. Keep the host
  retry for bases in the field on older firmware.
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
