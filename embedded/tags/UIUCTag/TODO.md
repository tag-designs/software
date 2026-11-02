---
type: worklist
status: current
summary: Open UIUCTag items -- external_pages clobbered by every reset, the debug port losing the system bus while the core sleeps in a session, a tag-test link failure probably fixed, and confirming the internal-ADC fix.
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
  attributed.** On 2026-11-02, main `12af820d` on the same
  `tag-breakout-base-l432-v1` (fw `be8f9062`, a dirty build of 2026-10-28)
  passed **six of six** with no 0x81 at all (the host still logs the first
  failure of a burst at ERROR, so none occurred). The L4 monitor fix, the
  host's SWD enter retry (2026-10-31), the host's `DBG_SLEEP` (2026-10-27) and
  possibly the base build changed since the failures below, so this does not
  say which one fixed it. Six of six against a 4-in-6 rate is p ≈ 0.09;
  delete this item after another clean dozen. Note that the base reports
  some failed accesses as successes (next item), so "no 0x81" is weaker
  evidence than it looks.

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

- **While the core sleeps during a monitor session, the debug port cannot
  reach the system bus, and the base reports the failed accesses as
  successes.** Every `tag-test` prints `GetStatus failed: Tag returned
  NANOPB` once, then finishes `ALL_PASSED`. Traced on 2026-11-02 with
  throwaway probes (none committed), on `tag-breakout-base-l432-v1`:
  - **Not the monitor.** An image with the L4 monitor fix reverted fails the
    same way. The tag answers `08 08`, `errAck(NANOPB)`: `proto_eval()` could
    not decode the request, because the first word of `ProtoBuf` was zero
    while the host had written `12 00` (`get_status`).
  - **The host's write never lands, and nothing says so.** Read back straight
    after writing, the buffer is `00 00 00 00`; a second write and read give
    the same. The base reported both as OK, including to the host's
    last-read/write-status check.
  - **Every system-bus access fails, not just SRAM.** In the same window
    `RCC_CR`, `RCC_CFGR` and `PWR_CR1` read 0 (with OK status), while the
    core-private registers answer: `DBGMCU_CR` = 1 (`DBG_SLEEP`, set by the
    host), `SCB_SCR` = 0 (no `SLEEPDEEP`), `DHCSR` = `0x01050001` (core in
    Sleep).
  - **Any sleep in the main thread does it; the ADXL367 is incidental.** A
    test that only calls `chThdSleepMilliseconds(1500)` fails the same way;
    the real ADXL367 test is simply the only one longer than the host's 1 s
    poll. With the accelerometer's USART2 bus up, or the sensor measuring,
    nothing changes. Ordinary session idle polls cleanly because on L4 the
    main loop never blocks during a session (`godown()` returns and the loop
    yields), so the core only reaches the BitPresTag idle hook's `__WFI()`
    while a test sleeps. A `Test` request's own reply was once read as zeros
    for the same reason: the test, and its sleep, starts the moment the
    reply is written.
  - **Not Stop mode.** `stopMilliseconds()` falls back to the RTOS sleep while
    the monitor is attached, and `SCB_SCR` = 0.
  - **The idle `__WFI()` is the necessary condition.** With the BitPresTag
    idle hook changed to skip `__WFI()` while `DEMCR.VC_CORERESET` is set, a
    1.5 s sleep-only test and the real ADXL367 test passed 6 of 6 with every
    request read back intact; with the hook as shipped, every one failed.
  - **Contradicts 2026-10-27.** That investigation read `PWR_CR1 = 0x500`
    and made 48 clean status reads across sleeps with `DBG_SLEEP` set, on the
    same base model (`../../bases/design/investigations/2026-10-0x81-dbg-sleep.md`).
    Still to find: why plain Sleep with `DBG_SLEEP` now cuts off the system
    bus, and why the base answers OK. One candidate for the second: a posted
    AHB-AP access that faults sets `STICKYERR` while its own ACK is OK.

  **Not the base build:** a clean `tag-breakout-base-l432-v1` (`5c6ddaf6`,
  flashed 2026-11-02) fails identically, with one 0x81 in six runs, which
  fits a posted AHB-AP access faulting on the target bus: the access's own
  ACK is OK, `STICKYERR` is set, and the next AP access gets the FAULT. Also
  ruled out: the Sleep clock gates (`RCC_AHB1SMENR` = `0x00011303`, the reset
  value, SRAM1 and flash clocked in Sleep), how `DBG_SLEEP` is set (the
  host and the 2026-10-27 tool write the same bit), and a missing barrier
  after the `WFI`: an idle hook of `__WFI(); __ISB();` (idle thread
  `wfi; isb sy`) still failed 6 of 6 with the sleep-only test. As expected,
  since the reads fail for the whole sleep and the `ISB` runs only after
  wake-up. The one recorded
  difference from 2026-10-27 is the supply: 3.28 V then, 2.496 V now. 2.5 V
  is this tag's operating voltage, so the tag must work there.

  Fixes, separable, none needing the cause:
  - **Firmware (batch):** skip the idle `__WFI()` while a monitor session is
    open, as the probe build did. On L4 the main loop already never blocks
    during a session, so the core only idles there while a test or delay
    sleeps; the cost is that time at run current instead of Sleep.
  - **Host:** read the request back after writing it and retry or fail
    loudly, rather than trigger on zeros.
  - **Base:** check `CTRL/STAT.STICKYERR` after a memory transfer, so a
    faulted access reports 0x81 instead of OK with zeros.

  The L4 mailbox in
  [the monitor proposal](../common/core/design/proposals/l4-monitor-robustness.md)
  would not avoid this: its block is in SRAM too. Any request that lands in
  the window is lost. The harness's one `Info
  failed` after `tag-start` in the 2026-11-02 release check may be the same.
  The host can at least detect it by reading the request back before
  triggering; the cause is a base or firmware item.

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
