---
type: procedure
status: current
summary: UIUCTag power test plan -- qualifying a release image for idle, running and finished current on a tag that wakes once a minute and stores one block every five.
---

# UIUCTag Power Test Plan

Qualifies a `UIUCTag` release image for the three currents that set a
deployment: `IDLE`, `RUNNING` and `FINISHED`. UIUCTag is an STM32L432 carrying
a BMP581 for pressure and an ADXL367 for activity, logging to AT25XE external
flash.

The rig, the Joulescope server, flashing, supply voltage and what to record are
in the shared
[power testing procedure](../../../../docs/bench/power-testing.md).
Measurements go in [`power-results.md`](power-results.md). Background on the
tag is in [`overview.md`](overview.md) and
[`data-collection.md`](data-collection.md).

**No baseline exists.** This is UIUCTag's first power qualification, so the
gates below are sanity bounds, not regression bounds, and the first clean
session sets the numbers everything later is compared against.

## 0. Inputs

```sh
RELEASE=<unpacked-release>/firmware        # contains UIUCTag/, ...
BOARD=<physical-label>
OUT=release-checks/uiuctag-$(date +%Y%m%d-%H%M%S)
CFG=embedded/proto-c/uiuctag-proto-c/default-config.json
mkdir -p "$OUT"
```

`CFG` is the shipped default. It already sets `active_interval` to
`0 .. INT32_MAX`, which matters: a config that omits it programs
`end_epoch = 0` and the run ends one second after it starts, which is how a
BitTag run was lost ([BitTag results](../../BitTag/design/power-results.md)).
There is nothing to sweep — the cadence below is compile-time.

## 1. What the firmware does, and how that sizes a window

| Constant | Value | Meaning |
| --- | ---: | --- |
| `UIUCTAG_ACTIVITY_BUCKET_SECONDS` | 60 s | one packed activity bucket |
| `UIUCTAG_ACTIVITY_BUCKETS_PER_EXTERNAL_BLOCK` | 5 | buckets per stored block |
| `UIUCTAG_EXTERNAL_BLOCK_SECONDS` | **300 s** | one stored record, five minutes |
| `UIUCTAG_LOG_SAMPLES` | 24 | records per raw download block |
| `UIUCTAG_DATA_LOG_SECONDS` | 7200 s | two hours per raw download block |

`Running()` arms `enableAlarm(0, ALARM_MINUTE)`, so **the MCU wakes once a
minute**, samples the ADXL367 wake line into a 60 s bucket, and writes an
external block every fifth wake. The ADXL367 also wakes the part on activity
edges, so a tag being handled wakes more often and draws more: every
measurement here is taken with the tag **mechanically undisturbed**, and the
result is the quiescent floor, not a field average.

The wake is the load, so a window of N minutes holds N or N+1 wakes and the
alignment error alone is **1/N** — the same arithmetic as BitTag:

| Window | Wakes | Alignment error | Stored blocks |
| ---: | ---: | ---: | ---: |
| 300 s | 5 | 20% | 1 |
| 1200 s | 20 | **5%** | 4 |
| 1800 s | 30 | 3.3% | 6 |

**Use 1200 s**, which matches the 5% agreement band below and still produces
four downloadable blocks.

### What is powered in each state

| State | ADXL367 | MCU |
| --- | --- | --- |
| `IDLE`, `FINISHED` | shut down | Standby |
| `RUNNING` | activity detection, always on | Standby between minute wakes |

`IDLE` and `FINISHED` route through the same `tagPowerEnterTerminalSleep()` in
`common/core/src/pwr-l432.c`, so they should **measure the same**. A difference
between them is a finding, not noise.

### Set `--idle-max-ua` between the two states, not above both

`tag_lifecycle_check.py --idle-max-ua` is both the bound a resting state must
come under *and* the bound the running state must come **over**, because a run
drawing idle current collected nothing. On CompassTag a value above both
declared a healthy 1.95 uA run asleep in the same pass whose download found 30
rows ([CompassTag results](../../families/CompassTag/design/power-results.md),
2026-10-04).

UIUCTag's resting floor measured **0.16 uA** before this plan was written and
its running current is unknown, so start at **`--idle-max-ua 1`** and check the
first result separates the states. If running comes in under 1 uA, lower the
bound below it rather than concluding the tag did not collect.

## 2. Before you flash

**There is no calibration to preserve.** `__calibration_start__` and
`__calibration_end__` are both `0x0800b800` in the released ELF — a zero-length
region. Unlike CompassTag, a mass erase costs nothing here beyond the power
cycle that `FLASH_SR.PEMPTY` forces, and `flash_release.py` clears that itself.

```sh
embedded/tools/flash_release.py "$RELEASE/UIUCTag" \
    --label "$BOARD" --json "$OUT/flash.json"
cp "$RELEASE/UIUCTag/UIUCTag-build-manifest.json" "$OUT/"
```

A manifest reporting `dirty` is not a release and must not be qualified.

**Supply: 2.5 V, unregulated**, as on the other L432 tags — the parts run
straight off the cell, so no voltage scaling applies and a figure holds only at
the voltage it was taken at (shared procedure §5). Confirm 2.45-2.55 V on the
Joulescope **before the tag is connected**; the bench is shared with IMUTag
work at 3.7 V, which is above the absolute maximum for parts fed directly from
the rail.

## 3. Phases

### Phase A — the three currents

```sh
embedded/tools/tag_lifecycle_check.py \
    --config "$CFG" --run-duration 1200 --rest-duration 120 --settle 75 \
    --idle-max-ua 1 --use-server --verbose
```

`--rest-duration 120` because the resting floor is sub-microamp and a 30 s
window is not enough charge to integrate against. `--settle 75` covers a full
minute alarm before the window opens.

### Phase B — running current, measured independently

```sh
embedded/tools/power_experiment.py \
    --config "$CFG" --duration 1200 --settle 75 --running-timeout 120 \
    --label uiuctag --use-server \
    --output "$OUT/running.csv" --keep-download "$OUT/download"
```

Require two windows within 5%, counting Phase A's. Confirm the state is
`RUNNING`, not `CONFIGURED`, before trusting either.

### Phase C — download and functional

```sh
build-host/bin/tag-dwnld -f sqlite -o "$OUT/download.db3"
build-host/bin/tag-reset --set-rtc    # FINISHED -> IDLE; erases, so download first
build-host/bin/tag-test               # expect RUN_ALL -> ALL_PASSED
```

`tag-test` sets the clock, which `FINISHED` does not permit, so it runs from
`IDLE` and therefore after the download.

Check the database: block timestamps **exactly 300 s** apart, pressure
plausible and non-saturated, and activity at or near zero throughout — correct
for an undisturbed tag, and **not** a test of the activity encoding, which
needs real motion.

### Phase D — a second attach pattern

Every resting figure above follows a `tag-reset`. Confirm the floor also holds
after a different attach:

```sh
build-host/bin/tag-test
sleep 15
embedded/tools/joulescope_measure.py --use-server --duration 120 --window 0.5
```

## 4. Gates

| Point | Gate | Rationale |
| --- | --- | --- |
| supply, every measurement | 2.45-2.55 V | unregulated, so a figure means nothing at another voltage |
| `IDLE` | ≤ 1 uA | a "did it sleep" bound against a ~0.16 uA floor |
| `FINISHED` | ≤ 1 uA, and within 20% of `IDLE` | same code path; a difference is a finding |
| `idle_after_cycle` | ≤ 1 uA | the same state by a second history |
| Phase D | at the Phase A floor | the floor must not depend on which request attached |
| `RUNNING` | two windows within 5% | recorded, not gated: no baseline yet |
| `tag-test` | `ALL_PASSED` | hard |
| download | block timestamps exactly 300 s apart | hard |

## 5. Not covered

- **The never-attached cold baseline.** Every tool attaches.
- **Sample-period sweep.** The cadence is compile-time; there is nothing to
  sweep.
- **Activity encoding under real motion.** A stationary bench tag reports no
  activity by design. Exercising the encoding needs controlled movement, which
  is a separate test.
- **Scheduled start and stop, and hibernation.** In the config, untested here.
- **Board-to-board spread.** One board, one UUID.

## 6. Recording results

Append each session to [`power-results.md`](power-results.md), recording what
the shared
[recording checklist](../../../../docs/bench/power-testing.md#7-recording-a-session)
asks for. A release qualification also goes onto the release page: see
*Publishing the qualification* in
[the release procedure](../../../../docs/release/release-procedure.md).
