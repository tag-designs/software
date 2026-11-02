---
type: worklist
status: current
summary: Open UIUCTag items -- external_pages clobbered by every reset, the debug port losing the system bus during the ADXL367 self-test, a tag-test link failure probably fixed, and confirming the internal-ADC fix.
---

# UIUCTag TODO

These come from sessions on the shared breakout board, UUID
`2036354B3032500800520028` -- the same physical unit the CompassTag log calls
`CompassTagAT25Breakout`. The "never wakes" report from the same sessions was
retracted; see
[the investigation](design/investigations/2026-10-uiuctag-wake-fault-retracted.md).
UIUCTag was qualified on `fw-v0.6` on 2026-10-08
([power results](design/power-results.md)).

Delete an item when it is done.

- **`tag-test` on `RUN_ALL` was intermittent; probably fixed, not yet
  attributed.** On 2026-11-02, main `12af820d` on a `bittag-base-v7` base
  passed **six of six**, with no 0x81 at all (the host still logs the first
  failure of a burst at ERROR, so none occurred). Three things changed since
  the failures below: the L4 monitor fix, the host's SWD enter retry
  (2026-10-31) and the base firmware, so this does not say which one fixed
  it. Six of six against a 4-in-6 rate is p ≈ 0.09; delete this item after
  another clean dozen.

  Re-measured on
  `fw-v0.6`, 2026-10-06: **four of six attempts passed** `ALL_PASSED`,
  including the ADXL367 self-test. The two failures died with
  `LIBUSB_ERROR_TIMEOUT` and "empty or invalid acknowledgement" immediately
  after `Running test RUN_ALL` and before the first `test: start request=3`,
  so before any device test ran.

  **The `read debug register 0xe000edfc failed status=0x81` errors are not the
  signal.** They appear 61-64 times in every attempt, including all four that
  passed. The first report treated them as evidence; they are
  background noise on this rig. What distinguishes a failure is only the USB
  timeout at the first transaction of the sequence, which points at the link
  rather than at the tag.

- **During the ADXL367 self-test the debug port loses the system bus, and a
  status poll fails with `Tag returned NANOPB`.** Every `tag-test` prints
  `GetStatus failed: Tag returned NANOPB` once, then finishes `ALL_PASSED`.
  Run one test at a time, only `RUN_ADXL362` (the ADXL367 test) does it.
  Traced on 2026-11-02 with throwaway probes (none committed):
  - **Not the monitor.** An image with the L4 monitor fix reverted fails the
    same way. The tag answers `08 08`, `errAck(NANOPB)`: `proto_eval()` could
    not decode the request, because the first word of `ProtoBuf` was zero
    while the host had written `12 00` (`get_status`).
  - **The host's write never lands.** Read back straight after writing, the
    buffer is `00 00 00 00`, and a second write and read give the same. The
    ST-LINK memory commands report no error, so the failure is silent.
  - **Every system-bus access fails, not just SRAM.** In the same window
    `RCC_CR`, `RCC_CFGR` and `PWR_CR1` read 0 over the debug port, while the
    core-private registers answer: `DBGMCU_CR` = 1 (`DBG_SLEEP`, set by the
    host), `SCB_SCR` = 0 (no `SLEEPDEEP`), `DHCSR` = `0x01050001` (core in
    Sleep).
  - **Not Stop mode.** The test waits with `chThdSleepMilliseconds()`, and
    `stopMilliseconds()` falls back to that while the monitor is attached.
    The difference from ordinary session idle, which polls fine, is that the
    accelerometer's bus is up: USART2 in synchronous mode on PA1-PA4
    (`TAG_BUS_USART_INIT` in `src/devices.c`, `common/core/src/usart_bus.c`).
    Still to find: what about that state cuts the AHB off from the debug
    port while the core sleeps.

  Harmless to `tag-test`, which polls again, but any request that lands in
  the window is lost, and the harness's one `Info failed` after `tag-start`
  in the 2026-11-02 release check may be the same thing. Two remedies,
  separable: the host can check the probe's last read/write status after
  writing the request and retry, which is a host change; the cause is a
  firmware item for the batch.

- **`external_pages` is wrong after any reset, and reports nothing collected.**
  Confirmed on hardware 2026-10-06: a run that stored **5 pressure samples, 5
  temperature samples and 20 activity buckets** reported `external_pages=0` in
  the same session whose download returned all of them.

  `restoreLog()` in `src/datalog.c` does, on every recovery:

  ```c
  pState->external_blocks = pState->pages;   /* "conservative lower bound" */
  ```

  `external_blocks` is the running **sample** count, set per sample write in
  `Running()`. `pages` is the **checkpoint** count -- one per block, and a block
  is 24 samples. So recovery replaces a correct value with one up to 24x
  smaller, and with **zero** for any run that has not yet completed its first
  block, which at a 300 s sample period is the first two hours of every run.

  It is reached constantly, because **attaching is a reset**: stopping a tag to
  read its status runs recovery first, so the counter is clobbered on the way to
  every reading a human takes.

  Two things to fix, and they are separable:
  - The counter should survive, or be recomputed, rather than be replaced by a
    quantity measured in different units. `pState` is backed by the RTC backup
    registers and survives reset, so the real value is available.
  - The comment claims the seeded value is "still a valid, if stale, download
    bound for the host". **Check whether anything actually bounds a download by
    it.** If so, a reset mid-run could truncate a download and silently lose
    data -- far worse than a cosmetic counter. The 2026-10-06 download returned
    everything, so nothing bounded it there, but that is one observation.

  Until it is fixed, **never read `external_pages` as evidence of what a run
  collected -- use the download.** Misreading it that way is a large part of how
  this tag was wrongly written off for two days.

- **Confirm the internal-ADC fix holds here.** This board is what exposed the
  `adcVDD()` defects: it reported `vdd=3.77-3.96` and `temp=92.5-111.4` while
  the rail was 2.4960 V and the room about 25 C. Both outputs come from one
  call and the temperature is computed from the voltage, so one unsettled
  VREFINT sample corrupted both. The same code reads correctly on BitTag and
  CompassTag, so the margin was board-dependent rather than absent. After the
  fix this board read 2.48-2.49 V and 23.9-25.3 C; that was two samples, and
  wants confirming over a longer run and against the BMP581's own temperature.
- **Find the shortest sampling time that still reads correctly.** 640.5 cycles
  was chosen to remove the clock dependency, not because it was measured to be
  necessary. The settling delay may be doing all the work. Sweep it on this
  board; the harness is `adc_sweep.sh` from the 2026-10-04 session.
