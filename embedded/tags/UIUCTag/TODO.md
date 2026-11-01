---
type: worklist
status: current
summary: Open UIUCTag items -- external_pages clobbered by every reset, an intermittent tag-test link failure, and confirming the internal-ADC fix.
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

- **`tag-test` on `RUN_ALL` is intermittent, not reproducible.** Re-measured on
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
