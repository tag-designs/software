---
type: worklist
status: current
summary: Open UIUCTag items from the 2026-10-04 bench session -- a run that stored nothing, a self-test that fails, and the internal-ADC fix that came out of it.
---

# UIUCTag TODO

Opened 2026-10-04. All of these are from one session on the shared breakout
board, UUID `2036354B3032500800520028` -- the same physical unit the CompassTag
log calls `CompassTagAT25Breakout`. It is a development board, not production
UIUCTag hardware, so none of this is a qualification result.

- **A run stores nothing because the tag never wakes. Reproduced on
  `fw-v0.6`, 2026-10-06.** This supersedes the description below, which was
  wrong in its arithmetic and incomplete in its diagnosis.

  A 27-minute run on board `2036354B3032500800520028`, `fw-v0.6` (`56e5e6a0`),
  flashed from the release: `external_pages` stayed **0** at T+3, 8, 14, 20 and
  27 minutes. At a 300 s sample period that is about five samples missing. The
  tag reported `RUNNING` throughout and drew **0.2347 µA** over a 60 s window --
  resting current. It was asleep for the whole run.

  So nothing is wrong with the write path, the cursor or the download. **The
  sample wake never happens.**

  What is established:
  - The RTC is running and accurate: `ppm_clock_error` 0.95, one LSB.
  - A capture taken while the fault was live
    (`captures/capture-20261006-161712`) reads `RTC_ALRMAR = 0x80808000`, which
    is exactly the mask `enableAlarm(0, ALARM_MINUTE)` writes -- so the alarm
    *was* programmed and `Running(T_INIT, ...)` did run.
  - The same capture reads `RTC_CR = 0x00000020`: only `BYPSHAD`. **`ALRAE`,
    `ALRAIE` and `WUTE` are all clear**, so nothing was armed to wake the tag.
  - ChibiOS `rtc_lld_set_alarm()` (RTCv3) sets both `ALRAE` and `ALRAIE` when
    given a non-NULL spec, so the alarm was enabled and then disabled again.
  - No sample was ever written, not even the first. The design has the first
    minute alarm write sample 0, so the failure is before the first wake, not a
    stall part-way through.

  What is **not** established, and should be settled first: whether
  `ALRAE = 0` is the live value or an artifact of `tag-capture` connecting
  under reset. The RTC sits in the backup domain and a system reset should not
  clear `RTC_CR`, which is why the reading is believed -- but it has not been
  controlled for. **The control is cheap: capture a tag that is demonstrably
  waking** (a CompassTagAT25 mid-run wakes every 30 s) and check whether its
  alarm-enable bit survives the same capture. If it does not, the register
  evidence here means nothing and only the behavioural evidence stands.

  The leading suspect is `disableAllAlarms()`. `rtc_api.h` documents that it
  "clears Alarm A on every state entry", `main.c` calls it in boot cleanup and
  `state_run.c` calls it at lines 284 and 335. UIUCTag is the only target that
  depends on **Alarm A** for its run wakes (`enableAlarm(0, ...)`); the others
  use Alarm B or the ticker, which would explain why only this target is
  affected. Not yet confirmed: the `main.c` call is guarded by
  `if (power_init || force)`, so it does not obviously run on a mid-run wake.

- ~~**A 20-minute run stored nothing.**~~ **Superseded; the arithmetic was
  wrong.** It claimed the run "should have written four blocks". A block is 24
  samples at 300 s each -- a **7200 s** block period -- so 20 minutes is four
  *samples*, four of twenty-four slots in the first block, and zero completed
  blocks. `external_pages` counts samples rather than blocks in any case, with
  a comment in `state_run.c` saying it was changed for exactly this confusion.
  The trap is the macro name: `UIUCTAG_EXTERNAL_BLOCK_SECONDS` is 300, the
  sample period. The observation was still a real fault, which the entry above
  reproduces and localises. On `fw-v0.5`, a run from 20:55:25 to
  21:18:36 ended with `external_pages=0` and `pState->pages == 0`, and the
  download contained only `info`, `schema_info`, `streams` and `states` -- no
  data tables at all. At a 300 s block period that run should have written
  four blocks. This is not the download hiding a partial block: `data_logAck()`
  trims trailing unwritten slots and returns what a partial block holds, and
  it refuses only when `index >= pState->pages`. Unexplained.
- **`tag-test` on `RUN_ALL` is intermittent, not reproducible.** Re-measured on
  `fw-v0.6`, 2026-10-06: **four of six attempts passed** `ALL_PASSED`,
  including the ADXL367 self-test. The two failures died with
  `LIBUSB_ERROR_TIMEOUT` and "empty or invalid acknowledgement" immediately
  after `Running test RUN_ALL` and before the first `test: start request=3`,
  so before any device test ran.

  **The `read debug register 0xe000edfc failed status=0x81` errors are not the
  signal.** They appear 61-64 times in every attempt, including all four that
  passed. The original entry below treated them as evidence; they are
  background noise on this rig. What distinguishes a failure is only the USB
  timeout at the first transaction of the sequence, which points at the link
  rather than at the tag.

- ~~**`tag-test` fails reproducibly on `RUN_ALL`.**~~ **Superseded: it is
  intermittent.** Three consecutive attempts,
  each dying with `read debug register 0xe000edfc failed status=0x81`,
  `LIBUSB_ERROR_TIMEOUT` and `Tag returned an empty or invalid
  acknowledgement`. Not the transient link fault this rig shows from time to
  time. Whether this and the empty run share a cause is unknown; they were
  found together and should be investigated together.
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
- **Verify the stored-configuration write.** UIUCTag is one of the seven
  targets whose `writeStoredConfig()` ignores the result -- see
  [the proposal](../design/proposals/stored-config-write-is-unchecked.md).
  Worth ruling in or out as a cause of the empty run above, since a refused
  config write leaves the tag running something nobody chose.
- **No power qualification exists.** The plan is
  [`design/power-test-plan.md`](design/power-test-plan.md) and the log is
  [`design/power-results.md`](design/power-results.md), both written
  2026-10-04 and neither yet carrying a passing session. The resting currents
  measured on this breakout -- `IDLE` 0.1628 µA, `FINISHED` 0.1640 µA,
  `RUNNING` 0.2466 µA -- are recorded here only as orientation, not as a
  baseline: a breakout is not the hardware that flies.
