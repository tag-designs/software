---
type: results
status: current
summary: Append-only log of CompassTag power measurements -- the Standby-after-attach fault before and after its fix, and the production CompassTagAT25 validation.
---

# CompassTag Power Measurement Log

**Append-only.** Each completed measurement gets one entry with a timestamp,
the conditions it was taken under, and the numbers. Never edit or delete an
entry — a measurement that turned out to be wrong gets a later entry saying
so, because the wrong ones are how the reasoning is reconstructed.

Procedure: [`power-test-plan.md`](power-test-plan.md), and the shared
[power testing procedure](../../../../../docs/bench/power-testing.md). The
fault these entries chase is written up in
[the investigation](investigations/2026-09-compasstag-standby-after-attach.md);
open items are in [`../TODO.md`](../TODO.md).

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
  Standby-decline erratum — see
  [the investigation](investigations/2026-09-compasstag-standby-after-attach.md)
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

### 2026-09-22  Session summary — fix verified on `CompassTagAT25Breakout`

Merged from the session report. The individual measurements are the entries
above; this records the session's provenance and verdict.

#### Provenance

| Item | Value |
| --- | --- |
| Date (UTC) | 2026-09-22 |
| Operator | G. Brown / Claude (Claude Code) |
| git hash | diagnosis began at `379e3f1`; fix committed as `3ca3f99`; tooling fix `ef6033d` |
| **Tree dirty?** | during diagnosis yes (temporary GPIO diagnostic instrumentation, reverted); final confirmation measurement (§ results, ~19:25 entry) taken on the clean committed tree |
| Target built | `CompassTagAT25`, `CompassTagAT25Breakout` |
| Board / UUID | `203633324B425006004A005D` (plain CompassTagAT25, pre-fix repro only); `2036354B3032500800520028` (CompassTagAT25Breakout, full pre-fix repro + fix verification) |
| Supply | Joulescope JS220, ~2.485 V |
| Joulescope interpreter | `~/opt/joulescope-mcp/.venv/bin/python` |
| Joulescope server used? | yes, `joulescope_server.py --start`, one server held for the relevant windows |
| **Joulescope desktop app detached?** | yes (confirmed by operator before handing the instrument over) |
| **`qtmonitor` detached?** | yes (confirmed by operator) |
| Plan deviations | Phase B run with two attach patterns (`tag-reset`, `tag-test`) rather than an exhaustive sweep of every RPC path; PA10/11/12 (§6 of the plan) available on the breakout board but not used — the git-history diff evidence was conclusive before logic-analyzer correlation was needed |

#### Phase B — repeated attach patterns

| pattern | result | gate | verdict |
| --- | --- | --- | --- |
| `tag-reset --set-rtc` | 0.3786 µA | matches never-attached baseline | **pass** |
| `tag-test` (RUN_ALL + GetTagInfo + SetRtc) | 0.3789 µA | matches never-attached baseline | **pass** |

Never-attached baseline (operator-measured, cold power-up, no probe ever
connected): **376 nA**. Both attach patterns land within ~1% of it.

#### Phase C — regression sanity

`tag-test` reported `Test Result: ALL_PASSED` (RTC, magnetometer, accelerometer,
external flash) both before and after the fix — the change is confined to the
`DBGMCU->CR` path in `tagPowerEnterTerminalSleep()` and touches nothing else.

#### Gate summary

All gates in the plan's gates met on `CompassTagAT25Breakout`. The fix restores
the never-attached idle-current baseline after every attach pattern tested.

The items this session left open are in [`../TODO.md`](../TODO.md).

### 2026-09-24  `CompassTagAT25` on real production hardware — full validation, two fixes plus the 30 s interval change

- **build**: `4160d1e` (committed): the `DBGMCU->CR` unconditional-clear fix
  (`42a4a618`), the magnetometer SPI pull-down fix (`4160d1e`), the LIS2DU12 wake-threshold
  rework, and the 30 s compass sample interval, all together for the first
  time on this hardware
- **board**: `CompassTagAT25` target on the real production CompassTag unit
  (not the Breakout board this whole log otherwise covers), UUID
  `203633324B425006004A005D`, real user calibration present throughout
  (verified byte-identical before/after every flash via a `.calibration`
  section backup at `0x0800a800`, 2016 bytes — not touched by any of
  tonight's flashes)
- **conditions**: `tag-test` self-test, then a real `tag-start --set-rtc
  --start-now` run, 3 min (six 30 s sample ticks), `tag-dwnld --stop`, then
  `tag-reset`; power measured via `joulescope_measure.py`, no debug-port
  contact during any measurement window
- **result**:
  | condition | result |
  | --- | --- |
  | self-test | `ALL_PASSED` |
  | RUNNING current, 180 s window | **1.9538 µA** |
  | IDLE current, post-run, window 1/2 | **0.2346 µA** |
  | IDLE current, post-run, window 2/2 | **0.2317 µA** |
  | sample timestamps | all six exactly 30 s apart |
  | accel/mag values | stable, non-zero, non-saturated (tag stationary: az
    ~960-969, ax/ay noise-level; mag axes stable across all six samples) |
  | activity | 0.0 throughout (tag stationary — expected; not a test of the
    activity encoding itself, which needs real motion) |
- **notes**: this is the first test of the real production board specifically
  (not the Breakout dev board this whole log otherwise covers), and the first
  test of all of tonight's changes together. IDLE current (~0.23 µA) is
  higher than the Breakout board's own post-fix numbers above (~0.38 µA
  there is actually higher, so this compares favorably) but is not directly
  comparable to those entries: this run additionally has the magnetometer
  pull-down fix applied, which the Breakout board's own idle numbers above
  predate and don't need in the first place (its `MAG_PWR` genuinely cuts I/O
  power). RUNNING current (~1.95 µA) lines up closely with the ~1.97 µA
  predicted in the 30 s interval change design work. Two independent 60 s
  IDLE windows agreed closely (0.2346 vs 0.2317 µA), and calibration was
  confirmed byte-identical before and after flashing, so none of this
  required re-running `tag-cal`/`qtcalibrate`.

### 2026-10-04  `CompassTagAT25` release qualification, fw-v0.5 — first from a release image

- **build**: `fw-v0.5` (`fdcec161`), tree clean, toolchain 14.2.1.
  `CompassTagAT25.bin` sha256
  `87ec266e9f0cc1fd676132de936e1d9714bee5f13f99148c8844660cae125d19`,
  flashed with `flash_release.py` and verified against its manifest.
  **The first CompassTag figures taken from a release image rather than a
  local build.**
- **board**: `CompassTagAT25` on CompassTagv1 hardware, UUID
  `203633324B4250060022005E` — **a different unit** from the
  `...004A005D` of 2026-09-24. Mass-erased at the operator's instruction and
  recalibrated by hand before the run, so the usual "calibration byte-identical"
  gate does not apply to this session.
- **conditions**: supply 2.4960 V throughout, unregulated 2.5 V cell. Shipped
  default config, 30 s compass period. `joulescope_server.py --use-server`,
  qtmonitor and the desktop app detached. Resting windows 120 s, running
  windows 900 s (thirty ticks, so 3.3% alignment error).
- **result**:

  | State | Current (uA) | How |
  | --- | ---: | --- |
  | `IDLE`, clock set | 0.23 | life-cycle, both runs |
  | `RUNNING` | **1.96 / 1.95 / 1.9482** | three 900 s windows, two harnesses, 0.60% spread |
  | `FINISHED` | 0.22 | life-cycle |
  | `IDLE` after a full cycle | 0.22 | life-cycle |
  | resting after a `tag-test` attach | 0.2201 | Phase D, a second attach pattern |

  Resting mean **0.2225 uA** over four states, 10 nA spread. Running mean
  **1.9527 uA**, 8.8x resting. Per mAh of cell: 187 days resting, 21.3 days
  recording.

  `tag-test`: `ALL_PASSED`. Downloads: 30 `Activity` rows per run, every
  delta **exactly 30 s**.
- **verdict**: **PASS**, every gate in
  [`power-test-plan.md`](power-test-plan.md) met. **`FINISHED` had never been
  measured on a CompassTag before this session**; it matches `IDLE` to 0.01 uA,
  which is what sharing `tagPowerEnterTerminalSleep()` requires.
- **notes**: agrees with the 2026-09-24 production unit (idle 0.2346/0.2317,
  running 1.9538) on a different board — the first board-to-board agreement
  CompassTag has had, which the plan warned not to assume. Read the older
  1.9538 figure as carrying 17% of its own alignment error: it came from a
  180 s window, six ticks.

  Two tooling facts established. CompassTag does **not** hit the
  attach-from-sleep failure that stops `tag_lifecycle_check.py` driving a
  BitTag; it drove this tag cleanly. And the first run of this session
  **failed spuriously**: `--idle-max-ua` is both the resting bound and the
  bound the run must exceed, and at 5 uA it declared a 1.96 uA run asleep in
  the same pass whose download check found 30 rows. The plan now specifies
  `--idle-max-ua 1`. That log is kept as `lifecycle-idlemax5.log`.
- **artifacts**: `release-checks/compasstagat25-20261004-143226/`

### 2026-10-05  Spot check after the ADC fix and the LSE-bypass board change

- **build**: `9a62336a`, local build, clean rebuild of `CompassTagAT25`
  (`build/` and `dep/` removed first). Carries the `adcVDD()` fixes from
  `54135465` — the SMPR2 channel index corrected and the 200 us settling delay
  restored, internal channels at 247.5 cycles — and the `CompassTagv1` board
  change to `STM32_LSE_BYPASS` from `bac55007`.
- **board**: `203633324B4250060022005E` on CompassTagv1 hardware -- the same
  unit as the 2026-10-04 qualification. **Read back on 2026-10-05, after the
  fact**, from a board still attached and still reporting `githash 9a62336a`,
  so the attribution is sound; it was not captured during the session itself,
  which is the defect. An entry that cannot name its board is what made
  PresTag's 2026-09 resting figures unattributable and finally unusable.
- **conditions**: supply 2.4960 V, unregulated 2.5 V cell, shipped default
  config (30 s compass period). `joulescope_server.py --use-server`, qtmonitor
  and the desktop app detached. One 120 s resting window.
- **result**:

  | Check | Result | Against |
  | --- | --- | --- |
  | Boots with LSE bypass | **yes**, `tag-info` reads `githash 9a62336a`, state `IDLE` | first of the four re-configured boards verified |
  | ADC markers | **vdd 2.48 V, temp 22.7–23.0 °C** | truth 2.4960 V, room ~23 °C |
  | `IDLE`, clock set | **0.2372 µA** | 0.23 µA at the 2026-10-04 qualification |

- **verdict**: no regression, and the two changes that carried the most risk
  are clear. The LSE bypass mattered most: `bac55007` set it on every
  distributed board, and a missing `STM32_LSE_BYPASS` hangs the first boot
  forever in `__early_init`, with a monitor-attach timeout as the only
  symptom. This is the first of those four boards shown to boot. The ADC
  markers are 0.6% low on VDD and within a degree on temperature, consistent
  with what the same fix gave on UIUCTag and BitTag.
- **notes**: **this is a spot check, not a qualification.** One resting window,
  no run current, no life cycle, no attach storm, no download check, and a
  local build rather than a release image. `CompassTagAT25` still needs a full
  `tag_release_check.py` pass against the next release image, along with the
  other four targets — every distributed image changed.

### 2026-10-05  `CompassTagAT25` release qualification at `630ce14e` — PASS

- **build**: `630ce14e`, **tree clean**, built and flashed by
  `tag_release_check.py`. Carries the `adcVDD()` fixes (`54135465`), the
  `CompassTagv1` LSE-bypass board change (`bac55007`) and the life-cycle bound
  passthrough this session added. Artifacts, including the ELF and every log
  and database, in `release-checks/release-CompassTagAT25-20261005-180824/`.
- **board**: `203633324B4250060022005E` on CompassTagv1 hardware, read back from
  the tag. Calibration dates from the 2026-10-04 recalibration.
- **conditions**: supply **2.4960 V** throughout, unregulated 2.5 V cell.
  Shipped default config, 30 s compass period. `joulescope_server.py
  --use-server`, qtmonitor and the desktop app both verified absent by process
  check rather than assumed.
- **invocation** — the bounds matter more than usual here, see the note below:

  ```sh
  embedded/tools/tag_release_check.py --target CompassTagAT25 \
      --config embedded/proto-c/compasstag-proto-c/default-config.json \
      --idle-max-ua 1 --run-max-ua 5 \
      --run-duration 900 --rest-duration 120 --settle 60 --storm-sets 3
  ```

- **result**:

  | Point | Gate | Measured | Verdict |
  | --- | --- | ---: | --- |
  | supply, every window | 2.45–2.55 V | 2.4960 V | **pass** |
  | `IDLE`, four trials | ≤ 5 µA | 0.2327 / 0.2316 / 0.2314 / 0.2306 µA | **pass**, 0.9% spread |
  | `IDLE`, life cycle | ≤ 5 µA | 0.22 µA | **pass** |
  | `RUNNING` | two windows within 5% | **1.94** and **1.9387 µA** | **pass**, 0.07% apart |
  | `FINISHED` | ≤ 5 µA, within 20% of `IDLE` | 0.22 µA | **pass**, equal to idle |
  | `idle_after_cycle` | ≤ 5 µA | 0.22 µA | **pass** |
  | attach storms | every round survives | 3 sets, 6 rounds, 240 cycles, 0 aborted | **pass** |
  | `tag-test` | `ALL_PASSED` | `ALL_PASSED` | **pass** |
  | download interval | samples exactly 30 s apart | 30 s in all six stored rounds | **pass** |
  | calibration | survives the flash | intact and meaningful, see below | **pass** |

- **`FINISHED` has a number for the first time.** The plan added it because the
  2026-09-24 session never measured it. At 0.22 µA it is indistinguishable
  from `IDLE`, which is what the shared code path predicts.

- **The run is the measurement the harness could not previously take.**
  `tag_release_check.py` forwarded no `--idle-max-ua`, so the life-cycle step
  ran at its 100 µA default. That threshold also sets the floor the *run* must
  clear, so a 1.94 µA run was declared asleep and failed. Fixed in `630ce14e`;
  this is the first CompassTag qualification the release path could produce at
  all. The same defect excluded BitTag, PresTag and UIUCTag.

- **Calibration was checked by hand, because the harness does not check it.**
  `__calibration_start__` was confirmed still at `0x0800b000` — the address the
  board was calibrated under, with the image ending at `0x0800ac58`, 936 bytes
  of headroom — **before** the flash was allowed. Afterwards the page was
  dumped and decoded rather than merely counted: 56 bytes of payload against an
  otherwise erased page, carrying a signature word, four offset/scale terms and
  a symmetric soft-iron matrix with a near-unity diagonal:

  ```
   0.994955  -0.009387   0.002204
  -0.009387   0.956864  -0.031607
   0.002204  -0.031607   1.051520
  ```

  "Not blank" would not have been enough: a byte-level restore on 2026-10-04
  gave a byte-identical page and calibration that did not work.

- **The 30 s interval was verified under storm, not just at rest.** All six
  stored round databases give exactly 30 s between consecutive `Epoch` values
  in both `Compass` and `Activity`, and each of those rounds absorbed 40
  attach/detach cycles. The timestamp column is `Epoch`; check which column a
  claim rests on before believing it.

- **verdict**: **PASS.** `CompassTagAT25` is qualified at `630ce14e` from a
  clean tree. Against the 2026-10-04 `fw-v0.5` qualification — idle 0.23,
  running 1.96/1.95/1.9482, finished 0.22 — nothing moved: idle is within
  0.5%, running within 1.1%, finished identical. Every distributed image
  changed between the two, so this is a requalification rather than a
  confirmation, and it is the first of the five targets to be redone.
