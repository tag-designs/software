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

> **REINSTATED 2026-10-06, after a controlled run. Read this first.** The
> withdrawal below was itself premature. A 1800 s run with **no monitor attach
> anywhere inside the window** -- reset, start, measure, stop, download, each
> command issued twice, nothing polled -- reproduces the fault:
>
> | | measured | bring-up, undisturbed, same board |
> | --- | ---: | ---: |
> | `IDLE`, clock set | 0.1581 uA | -- |
> | **`RUNNING`, 1800 s** | **0.2409 uA** | **0.76 uA** |
> | `FINISHED` | 0.1572 uA | -- |
> | `IDLE` after cycle | 0.1572 uA | -- |
> | `external_pages` | **0** | -- |
> | download | **"No log records to download"** | -- |
>
> Six samples were due in that window and none was written. The current
> corroborates it without reference to any register: the ~0.5 uA shortfall
> against bring-up is the sample writes that did not happen (248 uJ each), and
> the 0.083 uA that `RUNNING` sits above `IDLE` is the ADXL367 watching for
> motion. **The tag is armed for activity and not for time.**
>
> **The operator's "it wakes and writes" is consistent with this, not against
> it.** They reported it waking *when shaken* -- the ADXL367 activity line,
> which works. The RTC minute alarm is what does not fire. So the original
> diagnosis stands; what was wrong was only the method used to reach it.
>
> **The polling lesson stands on its own and must not be unlearned.** Attaching
> during a run resets the tag and is forbidden by
> `docs/bench/power-testing.md`; the earlier runs here did it and their numbers
> are worthless. Two separate things went wrong and only one has been fixed:
> the method, not the firmware.
>
> ~~**WITHDRAWN 2026-10-06, same day: the measurement interfered with what it
> measured.**~~ Everything below that reports "the tag never wakes" was taken
> with a harness that polled `tag-info` at intervals through the run. **A
> monitor attach connects under reset**, so each poll reset a RUNNING tag.
> `Running()` re-arms the sample alarm only in its `T_INIT` branch and on
> `State_EVENT_EXCEPTION`; a reattach takes neither, so after the first poll
> the alarm stays disarmed and the tag cannot wake again. The operator ran the
> same firmware undisturbed and reports it waking on its own.
>
> That accounts for every observation reported below without a firmware
> regression: `ALRAE = 0` and `ALRAF` never set, `external_pages` frozen at 0,
> and `50a80a83` "failing" although its own bring-up report documents it
> working on this same board. **The common cause is the harness, not the
> commit range.**
>
> What may still be real, and is worth keeping: **a monitor attach during
> RUNNING appears to leave the run unable to wake.** If that holds, any attach
> mid-deployment silently ends data collection, which is a serious fault in its
> own right -- but it is a different fault from the one described below, and it
> has not yet been confirmed by a controlled test. The test is an undisturbed
> run with a single status read at the end, compared against a run polled
> part-way through.
>
> **Confirmed by the operator, same day: an undisturbed run on `fw-v0.6` woke
> on its own and wrote.** So UIUCTag collects normally and there is no data
> fault. Both failures UIUCTag carried are accounted for -- this one by the
> polling, and `tag-test` by an intermittent link (four of six attempts passed,
> and the debug-register errors cited as its evidence occur just as often in
> the passing runs).
>
> **The original 2026-10-04 observation is suspect for the same reason.** It
> reported a 20-minute run ending with `external_pages=0` and a download
> holding no data tables. Whether that run was polled while it ran is not
> recorded. If it was, it has the same cause and there may be no UIUCTag data
> fault at all. **Establish that before deciding whether UIUCTag still carries
> a "do not fly" on the release page** -- it is currently marked so partly on
> the strength of that observation.
>
> The rule is not new. `docs/bench/power-testing.md` section 3 already says
> "Do not poll state during a run", and records a PresTag run polled eight
> times that stored 3 samples instead of about 30. It names the two methods
> that work: a current trace, which needs no attach, or reading the epochs back
> from the download afterwards. Both were available and neither was used.
>
> Nothing below should be used as evidence until that is done.

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

  **The `ALRAE = 0` reading is genuine, not a capture artifact.** This was
  listed as the open question and the capture answers it by itself:
  `RTC_ALRMAR` reads `0x80808000`, which is **not** a reset value -- after a
  backup-domain reset it would be `0x00000000`. The RTC sits in the backup
  domain and kept its firmware-written mask through the same capture that
  shows `PWR` reading post-reset defaults, so `RTC_CR` is equally retained.
  `ISR` bit 8 (`ALRAF`) is also clear, so **the alarm was armed, cleared, and
  never fired.**

  (A correction while reading: the L432 uses the **RTCv2** driver, not RTCv3 --
  the capture has `ISR` at offset `0x0C`, where RTCv3 has `ICSR`. RTCv2's
  `rtc_lld_set_alarm()` sets `ALRAE` and `ALRAIE` in the same way, so the
  conclusion is unchanged.)

  **Bisecting says the cause is probably not a firmware change since bring-up.**
  `50a80a83` -- the bring-up commit, whose own report documents the first
  sample write firing at minute 1 with qtmonitor detached, on **this same
  board**, UUID `2036354B3032500800520028` -- was rebuilt in a worktree and
  flashed, and it **fails identically**: `external_pages=0` after 150 s, tag
  `RUNNING`. The ADC markers read `vdd=3.88 temp=103.2`, confirming the image
  really is of that vintage.

  **That bisect has a confound which must be removed before trusting it.** The
  old firmware was driven by *today's* host tools, and `5060aa03` added the
  RV3028 clock offset to `t_storedconfig`. A layout mismatch would hand the old
  firmware a configuration it never saw at bring-up. The tag did accept the
  start and reach RUNNING, so the config is not wholly garbage, but that is not
  proof. Settle it by building `tag-start`/`tag-info` at `50a80a83` too and
  retesting, or by reading back the stored configuration and comparing it
  against what bring-up used.

  If `50a80a83` still fails with matching host tools, the change is **not in
  the firmware** and the search moves to the board files, the hardware, or the
  bench conditions -- the worktree at `/tmp/claude-1000/bisect-wt` and the
  harness at `/tmp/claude-1000/wake_test.sh` are set up to continue.

  What is **not** established, and should be settled first: ~~whether `ALRAE = 0` is the live value or an artifact~~ -- **answered
  above, it is live.** What remains open is the host-tool confound in the
  bisect. The RTC sits in the backup domain and a system reset should not
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
