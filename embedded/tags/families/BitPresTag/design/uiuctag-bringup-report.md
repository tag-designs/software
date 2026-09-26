# UIUCTag Bring-Up Report

First end-to-end bring-up of a fresh UIUCTag: self-tests, wake configuration,
real logged data, and an extended power measurement. One session per heading;
keep old sessions rather than overwriting them.

**Status: all six bugs below fixed and committed as `50a80a8`, verified on
real hardware with real logged data, one real shake event, and a per-event
wakeup-cost breakdown that reconciles with the extended average to within
~5%. Remaining open items are listed under "Not yet run".**

---

## Session 2026-09-25 / 2026-09-26

### Provenance

| Item | Value |
| --- | --- |
| Date (UTC) | 2026-09-25/26 |
| Operator | G. Brown / Claude (Claude Code) |
| git hash | `50a80a8` (all fixes below, committed together) |
| Target built | `UIUCTag` |
| Board / UUID | `2036354B3032500800520028` |
| Supply | Joulescope JS320, ~2.485 V |
| Joulescope interpreter | `~/opt/joulescope-mcp/.venv/bin/python` |
| Joulescope server used? | yes, `joulescope_server.py --start`, one server held for the extended measurement |
| **Joulescope desktop app detached?** | yes (confirmed by operator before the extended measurement) |
| **`qtmonitor` detached?** | yes (confirmed by operator) |

### Bugs found and fixed

1. **`RUN_LPS` self-test failing** — `bmp581_check_who_am_i_device()` used a
   single unreliable SPI read instead of the retrying `bmp581_init_device()`
   path. Fixed; confirmed `ALL_PASSED` on hardware.

2. **ADXL367 never woke on activity** — UIUCTag's wake configuration had been
   re-derived from the datasheet several times (referenced vs. absolute
   inactivity, two different threshold scale factors) and never reproduced
   BitTagNG's field-validated behavior. Fixed by porting BitTagNG's
   `initActivitySensor()` verbatim (same register values, same constants),
   changing only which interrupt pin carries AWAKE (INT1 on both boards, so
   in the end nothing needed to change there either). Confirmed wake-on-shake
   working.

3. **AT25XE external flash writes silently failing** —
   `at25xeUnprotect()` (runs on every flash wake to clear write protection)
   issued a Write Status Register command but returned without waiting for
   its own write cycle (tW, measured up to ~11.4 ms) to finish. The very next
   command — a sector erase — read the part's busy bit as still set and
   treated it as a permanently stuck chip, when it was its own prior command
   still completing. Fixed by polling for completion in `at25xeUnprotect()`;
   also widened the sector-erase completion poll (750 ms → 3 s) after
   repeated testing showed a residual ~4% failure rate at the original
   budget. 40/40 passes on the write-verify self-test after both fixes.

4. **Real checkpoint/sample writes never happened, even after fix #3** — a
   same-thread self-deadlock. BMP581 (pressure) and the external AT25XE flash
   share one SPI1 bus and one non-reentrant binary semaphore
   (`TAG_SPI1_DEVICE_DEFAULTS` sets `.mutex = &SPI1mutex` for both, and
   `HAL_USE_SPI` is `FALSE` for this family, so bus acquisition is a plain
   `chBSemWait()`). `dataLogWriteBegin()` woke the flash and held that mutex
   for the entire write session; calling `samplePressure()` while it was held
   made BMP581's own bus-acquire block forever on a semaphore only the same,
   now-blocked thread could ever release. Confirmed via targeted
   instrumentation: `samplePressure()` was entered but never returned, no
   write ever completed, and the tag never reached WFI again afterward —
   explaining both "no data is ever logged" and "current stays high after the
   first minute" as the same root cause. Fixed by sampling pressure before
   waking the flash, so the two bus sessions run sequentially instead of
   nested.

5. **`external_data_count` never advanced past the checkpoint count** —
   `open_block()` set `pState->external_blocks = pState->pages`, both of
   which only advance once per block (many samples each), so a caller
   watching write progress saw it freeze between block boundaries even
   though every sample write was succeeding. Fixed: `external_blocks` is now
   updated to the running sample count on every successful sample write, not
   just at block boundaries.

6. **ADXL367 activity pegged at maximum, or the wake line stuck asserted from
   boot** — two related findings:
   - The wake-line level used for `pState->lastactstart` was read once at the
     top of the wake handler, before `samplePressure()` and both flash
     writes, which can easily run tens of ms plus a deliberate write-rest
     delay; using that stale value at the end of the handler stretched every
     activity-triggered wake's "active" window all the way to that point
     regardless of what the sensor had actually done since. Fixed by
     re-reading the line right before the final `lastactstart` update
     (matching BitTagNG's own `checkActivitySensorAwake()` re-read in the
     same place, with the same justification in its own comment).
   - Separately, `ADXL367_DeinitDevice()` only disables/clears `POWER_CTL`,
     the interrupt maps, and `ACT_INACT_CTL` — it does not reset the chip's
     internal loop-mode activity/inactivity state machine, which is not
     powered from an MCU-controlled rail and so survives every MCU reset,
     reflash, and exception-recovery re-init that doesn't also power-cycle
     the sensor. Observed directly on the bench: the AWAKE line stayed
     asserted from boot across several MCU resets and only cleared after a
     real shake. Fixed by issuing a genuine chip-level software reset
     (`ADXL367_SoftwareResetDevice()`, already used by the self-test but
     never wired into the normal init path) at the start of
     `initDataCollection()`, with a 100 ms settling delay.

### Validation

| check | result |
| --- | --- |
| `tag-test` self-test | `ALL_PASSED` |
| AT25XE write-verify self-test, repeated | 40/40 pass after fixes #3 |
| First real checkpoint write | fires on the first RTC minute alarm, as designed (confirmed via `pState->pages`/`external_blocks`/`lastwrite` going from `INT_MAX`/0 to real values) |
| Downloaded pressure/temperature | ~991.4-991.5 hPa, ~24 °C across two samples — physically sane, stationary tag |
| Activity, undisturbed board | all five one-minute buckets in the first block read `0` |
| Activity, ~5 s real shake | isolated to one minute bucket (~11 raw seconds out of 60, i.e. ~18% — the decoded field is a percentage, not a raw count), buckets before and after the shake stayed at `0` |
| Wake line after fix #6 | correctly reads inactive from boot, no shake needed to "unstick" it |

### ADXL367 wake-mode sample-rate timing

Config round-trip is verified: the activity threshold and inactivity sample
count set in qtmonitor reach `TIME_INACT`/`THRESH_ACT` via the same
`UINT16SWAP` + `_H`-address register-write pattern BitTagNG uses for its own
field-proven thresholds (see `config.c`), with no scaling or truncation bugs
in the path.

However, the *measured* inactivity-declare floor (time from real stillness to
the AWAKE line deasserting, read directly off the Joulescope trace) comes in
shorter than the datasheet-nominal `N x 160 ms` prediction for
`WAKEUP_RATE=01` ("6 samples per second") at every configured sample count
tried:

| samples (N) | nominal (N x 160 ms) | observed |
| --- | --- | --- |
| 3 | 480 ms | ~300 ms |
| 6 | 960 ms | ~600 ms |
| 12 | 1920 ms | ~1300-1400 ms (two trials, same setting) |

The two trials at N=12 differ by ~100 ms with nothing else changed, so a
meaningful part of the shortfall is measurement noise from reading the
current trace by eye rather than a single clean deterministic ratio — the
data don't fit one fixed multiplicative or additive correction cleanly across
all three sample counts. The register write is verbatim-identical to
BitTagNG's own (field-proven) configuration, so this is not a UIUCTag-
specific bug; it reads as the true wake-mode sample rate running somewhat
faster than the 160 ms/sample datasheet nominal (roughly 100-130 ms/sample
fits the observed points within the noise band). Not chased further absent a
cursor-timestamped (rather than eyeballed) measurement — see "Not yet run".

### Extended energy measurement

- **conditions**: `tag-reset --set-rtc`, `tag-start --start-now`, then
  `joulescope_measure.py --use-server --duration 1500 --window 2` (25 minutes,
  spanning ~5 sample-write cycles), Joulescope desktop app and `qtmonitor`
  both detached
- **result**:

  | measurement | current |
  | --- | --- |
  | Isolated idle baseline, 5 s window, pre-measurement spot check | **0.4247 µA** |
  | 25-minute average, spanning ~5 wake+write cycles | **0.7891 µA** |
  | Supply | 2.4853 V, steady |

- **wakeup cost breakdown** (operator-captured, per-event, isolated from idle):

  | event | energy | duration | avg. power | avg. current @ 2.485 V |
  | --- | --- | --- | --- | --- |
  | Activity/inactivity transition (accelerometer wake) | 2.4 µJ | 2.4 ms | 1.00 mW | 402 µA |
  | First sample/checkpoint write (fires at minute 1) | 248 µJ | 118 ms | 2.10 mW | 846 µA |

  Reconciliation against the 25-minute average above: idle baseline alone
  over 1500 s at 0.4247 µA is ~1565 µJ; five write events (once per 5 min,
  matching the actual cadence) add 5 × 248 µJ = 1240 µJ; the undisturbed
  board should have contributed ~zero activity-transition events in that
  window. Total ≈ 2805 µJ / 1500 s ≈ 1.87 µW ≈ **0.75 µA** average — within
  ~5% of the measured 0.7891 µA, using only independently-measured
  per-event numbers and the known write cadence. This closes the "not yet
  run" gap noted below for the write-event cost; the idle-baseline and
  activity-transition figures are consistent with, though not a full
  substitute for, a longer multi-block run exercising more transitions.

- **notes**: the 25-minute average necessarily mixes the idle baseline with
  the periodic cost of the once-a-minute wake/activity check and the
  once-per-5-minutes sample/checkpoint write; the isolated 5 s idle spot
  check (0.42 µA) lands at the ~500 nA target the operator specified. The
  operator's own follow-up per-event captures (above) separate the two wake
  costs from idle and reconcile with this average to within ~5%.

### Deployment power projection — dark-eyed junco activity pattern

Field behavioral data (operator-provided) puts juncos at 10-15% active time,
with no data on individual active-bout duration. Worst case for wake-event
frequency is the shortest plausible bout, ~1 s, which (at ~15% duty cycle)
implies a cycle of roughly one active + one inactive segment every ~6-7 s,
i.e. two transitions per cycle — **12-18 activity/inactivity transition
events per minute**, the range the operator specified.

Projected average current, combining the per-event costs above with this
worst-case transition rate and the actual 5-minute write cadence (idle
baseline continues alongside events; their duration is negligible relative to
a minute so no double-counting correction is needed):

| activity transitions/min | idle | writes (amortized) | activity transitions | total/min | avg. current |
| --- | --- | --- | --- | --- | --- |
| 12 (low end) | 63.3 µJ | 49.6 µJ | 28.8 µJ | 141.7 µJ | **0.9505 µA** |
| 18 (high end) | 63.3 µJ | 49.6 µJ | 43.2 µJ | 156.1 µJ | **1.0471 µA** |

So the worst-case field deployment (frequent, brief activity bouts) is
projected at **~0.95-1.05 µA average**, roughly 20-33% above the undisturbed
bench figure (0.7891 µA) — driven almost entirely by transition *frequency*,
not by "how active" the bird is in aggregate: each transition costs only
2.4 µJ regardless of the resulting bout's length, so a bird that is active
15% of the time in a few long bouts costs far less than one that is active
15% of the time in many short ones. This is a bound, not a measurement: it
assumes the reported 10-15% duty cycle holds with the shortest plausible bout
length throughout, which is the intended worst case, not necessarily the
typical one.

#### Battery life on an 11 mAh cell

Simple capacity/current projection (no derating for self-discharge,
temperature, or non-ideal low-current capacity extraction):

| scenario | avg. current | projected life | margin vs. 1-year target |
| --- | --- | --- | --- |
| Bench baseline, undisturbed | 0.7891 µA | 581 days (1.59 yr) | 1.59x |
| Worst case, 12 transitions/min | 0.9505 µA | 482 days (1.32 yr) | 1.32x |
| Worst case, 18 transitions/min | 1.0471 µA | 438 days (1.20 yr) | 1.20x |

A full year of continuous operation on 11 mAh requires an average current no
higher than **1.2557 µA**. Even the high end of the worst-case junco activity
projection (1.0471 µA, 18 transitions/min) stays under that threshold with
~20% margin — so an 11 mAh cell is ample for a full year of data collection
under the worst-case activity assumption modeled here, not just under the
quiet bench condition.

### Gate summary

All six bugs above confirmed fixed on real hardware with real logged data,
not just self-test results. Idle current is at the target order of magnitude;
the extended average is consistent with idle plus expected periodic activity,
not a regression.

### Not yet run

- No genuine power-cycle (full supply removal) re-confirmation of the ADXL367
  soft-reset fix (#6) was done — the fix was verified by MCU-reset-only
  cycling on the bench, which is the condition that exposed the bug in the
  first place, but a tag deployed from a true cold power-up was not
  separately re-tested.
- Only one block's worth of samples (2 samples, 1 checkpoint) has been
  downloaded and inspected; a longer run spanning multiple blocks (testing
  block-boundary rollover, not just the first block) has not been done.
- A precise (cursor-timestamped, not eyeballed) measurement of the ADXL367
  wake-mode sample rate has not been done -- see "ADXL367 wake-mode
  sample-rate timing" above. The observed inactivity-declare floor is
  consistently shorter than the datasheet-nominal 160 ms/sample, but the
  eyeballed readings aren't precise enough to characterize the true rate
  beyond "somewhat under nominal."
