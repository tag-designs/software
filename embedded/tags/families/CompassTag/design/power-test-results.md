# CompassTag Power Measurement Log

**Append-only.** Each completed measurement gets one entry with a timestamp,
the conditions it was taken under, and the numbers. Never edit or delete an
entry — a measurement that turned out to be wrong gets a later entry saying
so, because the wrong ones are how the reasoning is reconstructed.

Companion files: [`power-test-plan.md`](power-test-plan.md) (procedure),
[`power-test-report.md`](power-test-report.md) (report form),
[`power-test-status.md`](power-test-status.md) (live handoff, overwritten).

## Format

```
### YYYY-MM-DD HH:MM  <short title>
- **build**: git hash, target, notable defines
- **board**: UUID, and which physical unit if it matters
- **conditions**: what was attached, settle time, window
- **result**: the numbers
- **notes**: anything that qualifies them
```

Rig, unless an entry says otherwise: CompassTagAT25Breakout, ST-Link,
Joulescope JS220 via `joulescope_server.py --use-server`, `charge/time`
figure, supply ~2.485 V, qtmonitor and Joulescope desktop app detached.

---

### 2026-09-22  Operator baseline, cold power vs. one attach — the finding that identified the bug
- **build**: `379e3f1` CompassTagAT25 (pre-fix; unconditional `DBGMCU->CR = 0`)
- **board**: CompassTagAT25Breakout, "used previously for tuning power"
- **conditions**: (a) power applied cold, monitor never attached; (b) same
  power-up, then one monitor attach + detach
- **result**:
  | condition | current |
  | --- | --- |
  | Cold power-up, never attached | **376 nA** |
  | After one attach + detach | **365 µA**, repeatable |
- **notes**: measured by the operator directly (not scripted). This is the
  result that identified the fault as attach-related rather than a silicon
  Standby-decline erratum — see [[compasstag-standby-decline-idle-current]]
  for the ruled-out alternative theories.

### 2026-09-22 ~16:50  Life-cycle sweep, pre-fix, CompassTagAT25 (plain, non-breakout board)
- **build**: `379e3f1` CompassTagAT25
- **board**: UUID `203633324B425006004A005D`
- **conditions**: `tag_lifecycle_check.py --use-server --run-duration 30`
- **result**:
  | state | current | verdict |
  | --- | --- | --- |
  | idle_prepared | 362.40 µA | FAIL (>100 µA threshold) |
  | running | 338.11 µA | pass |
  | stopped (FINISHED) | 365.91 µA | FAIL |
  | idle_after_cycle | 366.88 µA | FAIL |
- **notes**: current is dead flat across ten separate 2 s windows in a
  follow-up check (360.67–361.61 µA, <1 µA spread, no duty-cycling) — the
  signature that pointed toward a static leak/stuck-state rather than a
  periodic wake pattern.

### 2026-09-22 ~17:30  Repro on CompassTagAT25Breakout, pre-fix
- **build**: `379e3f1` CompassTagAT25Breakout + temporary GPIO diagnostic
  instrumentation (PA10/PA11/PA12, reverted before the fix commit)
- **board**: UUID `2036354B3032500800520028`
- **conditions**: `tag-reset --set-rtc`, 12 s settle, 20 s window
- **result**: 362.13 µA (consistent with the plain-CompassTagAT25 result above
  on a different physical board)

### 2026-09-22 ~19:05  Post-fix, single attach pattern (`tag-reset`)
- **build**: `3ca3f99`-equivalent working tree (fix applied, not yet committed
  at measurement time; committed immediately after as `3ca3f99`)
- **board**: CompassTagAT25Breakout, UUID `2036354B3032500800520028`
- **conditions**: `tag-reset --set-rtc`, 12 s settle, 20 s window
- **result**: **0.3786 µA** — matches the operator's 376 nA cold-boot baseline

### 2026-09-22 ~19:10  Post-fix, second attach pattern (`tag-test`)
- **build**: same tree as above
- **conditions**: `tag-test` (RUN_ALL, GetTagInfo, SetRtc — a heavier RPC
  session than `tag-reset`), 12 s settle, 15 s window
- **result**: **0.3789 µA**

### 2026-09-22 ~19:15  Post-fix, full life-cycle sweep
- **build**: same tree as above
- **conditions**: `tag_lifecycle_check.py --use-server --run-duration 20`
- **result**:
  | state | current | verdict |
  | --- | --- | --- |
  | idle_prepared | 0.38 µA | pass |
  | running | 173.36 µA | pass |
  | stopped (FINISHED) | 0.38 µA | pass |
  | idle_after_cycle | 0.38 µA | pass |
- **notes**: `PASSED: every rest state slept and the run collected`. This is
  the full reset→idle→start→run→stop→FINISHED→download→reset→idle cycle, not
  a single spot check — every terminal state the state machine actually
  visits was exercised and measured.

### 2026-09-22 ~19:25  Post-fix, final clean build confirmation
- **build**: `3ca3f99` (committed; temporary diagnostic GPIO instrumentation
  removed, clean rebuild)
- **conditions**: `tag-reset --set-rtc`, 12 s settle, 15 s window
- **result**: **0.3768 µA**
- **notes**: confirms the fix's effect is from the `DBGMCU->CR` change itself,
  not an artifact of the diagnostic instrumentation that was present in the
  build used for the two attach-pattern entries above.

### 2026-09-22 ~19:55  Post-fix, plain `CompassTagAT25` firmware on Breakout hardware
- **build**: `d9d76d0` tree, `CompassTagAT25` target (not `...Breakout`)
- **board**: CompassTagAT25Breakout hardware (I2C pin assignment mismatch
  expected — this target assumes the non-breakout wiring)
- **conditions**: `tag-test`, 12 s settle, 15 s window
- **result**: `RUN_ALL` → `RTC_FAILED` (expected: wrong I2C wiring assumption
  for this hardware, unrelated to the power fix); idle current **0.3762 µA**
- **notes**: this was a deliberate mismatched board/firmware combination, run
  only to confirm the `DBGMCU->CR` fix's *mechanism* is identical across
  CompassTag targets (it is — same shared `pwr-l432.c`, no target-specific
  branch), not to validate `CompassTagAT25` on its own hardware. Do **not**
  read the RTC failure as a regression; do not read the clean idle current as
  a substitute for testing `CompassTagAT25` on its own board. Breakout
  firmware reflashed immediately after to leave the board correctly
  configured.
