---
type: procedure
status: current
summary: BitTag power qualification -- what the firmware draws in each state, the configs, the phases and the pass/fail gates for IDLE, RUNNING and FINISHED at 2.5 V.
---

# BitTag Power Test Plan — Idle, Running and Finished Currents

BitTag (board `BitTagv6`, STM32L432) is qualified at **2.5 V** on three
currents: the two resting states, `IDLE` and `FINISHED`, which should match
each other, and `RUNNING`, measured over 1200 s windows because the tag wakes
once a minute. The first qualification passed at 0.122 uA resting and
0.508 uA running; the numbers are in [`power-results.md`](power-results.md),
and what went wrong getting them is in
[the investigation](investigations/2026-10-bittag-first-qualification.md).

This plan covers only what is BitTag-specific. The rig, the Joulescope server,
flashing, and what to record are in the shared
[power testing procedure](../../../../docs/bench/power-testing.md). Open work
is in [`../TODO.md`](../TODO.md).

## 0. Inputs

The procedure is the same for every release; only these change. Set them once
per session and the commands below work unaltered.

| Input | Shell name | How to obtain it |
| --- | --- | --- |
| Firmware release | `$RELEASE` | The unpacked release's `firmware/` directory, e.g. the contents of `tag-firmware-<tag>.tar.gz` from the GitHub release for that `fw-v*` tag |
| Board label | `$BOARD` | The tag's physical label. Nothing in the image or the programmer identifies which board is attached |
| Results directory | `$OUT` | A fresh directory per session; everything below is written there |
| Log format | `$CFG` | `bittag-bitpersec.json` for the qualification; see §4 |

```sh
RELEASE=<unpacked-release>/firmware        # contains BitTag/, PresTag/, ...
BOARD=<physical-label>
OUT=release-checks/bittag-$(date +%Y%m%d-%H%M%S)
CFG=embedded/tools/power-configs/bittag-bitpersec.json
mkdir -p "$OUT"
```

Nothing else in this document names a release. If a command here mentions a
version number, it is wrong — report it rather than editing the number in
place, because the same mistake will be in the other commands too.

## 1. What the firmware does, and why IMUTag's numbers do not transfer

BitTag is an ADXL362 activity logger. It records **bits**, not samples: the
accelerometer runs autonomously in motion-detect (`POWER_CTL_WAKEUP`) mode and
drives `AWAKE` onto `LINE_ACCEL_INT`, and the MCU samples that line once a
minute and packs the result into a word.

`Running()` in `src/bt_state_run.c` arms `enableAlarm(0, ALARM_MINUTE)`, so
**the MCU wakes once per minute in every log format**. What the format changes
is how many minutes of bits go into one stored record:

| `bittag_log` | chunk | chunks/word | bits/chunk | one record every |
| --- | ---: | ---: | ---: | ---: |
| `BITTAG_BITPERSEC` | 1 s | 60 | 1 | **60 s** |
| `BITTAG_BITSPERMIN` | 60 s | 10 | 6 | **600 s** (10 min) |
| `BITTAG_BITSPERFOURMIN` | 240 s | 8 | 8 | **1920 s** (32 min) |
| `BITTAG_BITSPERFIVEMIN` *(firmware default)* | 300 s | 7 | 9 | **2100 s** (35 min) |

`sample_period = chunk_period * chunk_number`, from the `chunks[]` table at the
top of `bt_state_run.c`.

Three consequences for measurement, each of which invalidates an IMUTag default:

- **A 60 s window is useless here.** IMUTag at 400 Hz is a continuous load, so
  60 s is a steady-state average. BitTag's load is one wake per minute, so a
  60 s window contains exactly one wake and the answer depends on where in the
  minute the window opened. Use **whole minutes**, and size the window from the
  precision wanted: a window of N minutes holds N or N+1 wakes depending on
  alignment, so the alignment error alone is **1/N**. Five minutes is 20%;
  twenty minutes is 5%. Pick N before picking a pass band, not after.
- **`--run-max-ua 760` is three orders of magnitude too loose.** That bound is
  an IMUTag figure. BitTag runs at about half a microamp
  ([results](power-results.md)).
- **The sample-count check switches itself off, correctly.** `config_odr_hz()`
  looks for `lsm6.odr`; BitTag has none, so `check_download()` skips the rate
  check and still applies its monotonic-timestamp check to the largest
  time-series table. That is the right behaviour — do not invent an
  `--expected-hz` for BitTag.

### What is powered in each state

`tagDevicesApplyPowerState()` in `src/devices.c` deinitialises the ADXL362 on
standby entry **unless the state is RUNNING**:

| State | ADXL362 | MCU | Expected shape |
| --- | --- | --- | --- |
| `IDLE` | shut down | L432 Shutdown | floor: Shutdown + RV3028 + leakage |
| `FINISHED` | shut down | L432 Shutdown | identical to `IDLE` — same code path |
| `RUNNING` | motion-detect, always on | Standby between minute wakes | floor + ADXL + one wake/min |

`inc/custom.h` sets every non-running state to `SHUTDOWN`; `Running()` returns
`STANDBY`.

`IDLE` and `FINISHED` should therefore measure **the same**, because they run
the same code. A difference between them is a finding, not noise.

### Motion changes the answer

`AWAKE` edges are a wakeup source as well as the minute alarm, so a tag that is
being handled wakes more often than once a minute and draws more. **Every
`RUNNING` measurement in this plan is taken with the tag mechanically
undisturbed**, and the result is the quiescent floor, not a field average.
Phase D measures the other end deliberately.

### Settle 75 s after a start

The start was measured as immediate: `CONFIGURED` and `RUNNING` fell in the
same second in all three starts of the first qualification. The source says it
could wait for the next minute alarm, because `TAG_CONFIGURED_IMMEDIATE_START`
is defined only by `IMUTagNand` and `IMUTagNandBmp581`. Why the hardware does
not wait is not understood
([investigation](investigations/2026-10-bittag-first-qualification.md)). So
allow for a 60 s wait in `CONFIGURED`. It would bias a measurement without
failing it:

- `power_experiment.py --settle` defaults to **5 s**, sized for a tag that
  begins collecting at once. On BitTag a window opened 5 s after start spends
  most of its first minute measuring `CONFIGURED`, which is not the state under
  test, and reports a running current that is too low. **Pass `--settle 75`**:
  one full minute alarm plus margin.
- `tag_lifecycle_check.py --settle` defaults to 12 s, for the same reason.
  **Pass `--settle 75` there too.**

A tag that reads as *lower* than expected in `RUNNING` is the signature of this
mistake. Confirm the state is `RUNNING` — not `CONFIGURED` — before trusting
any running figure.

## 2. Rig

### Set the supply to 2.5 V before the tag is connected

**BitTag has no regulator.** The STM32L432 and the ADXL362 run directly from
the cell, nominal **2.5 V**. Every current this plan measures is drawn straight
off that rail.

> **The bench is presently set to 3.7 V** for IMUTag work. Connecting a BitTag
> at that voltage is out of specification for both parts — the ADXL362's
> absolute-maximum supply is 3.6 V — and risks damaging the tag. **Set the
> supply to 2.5 V and confirm it on the Joulescope before the BitTag is
> powered.** This is the first step of any session, ahead of flashing.

```sh
embedded/tools/joulescope_measure.py --use-server --duration 5 --window 0.5
#   voltage  mean          :         2.5xxx V      <- confirm BEFORE connecting
```

Every measurement step below reports `voltage mean`. **Require 2.45-2.55 V on
each one and record it**; a figure taken at any other voltage is not
comparable and does not belong in the results log.

Two consequences of there being no regulator:

- **No supply-voltage scaling applies.** The `x (V1/V2)` reasoning used for
  IMUTag in `families/IMUTag/design/power.md` describes a buck
  converter drawing constant *power*. There is no converter here, so that
  arithmetic is simply wrong for BitTag. A BitTag current means nothing except
  at the voltage it was taken at.
- **Battery runtime is the direct quotient**, `capacity_uAh / current_uA`, with
  no efficiency term.

The firmware's own floor is **2.00 V**: `datalog.c` flags `LOGWRITE_BAT` when
`vdd100 < 200`. Note that the `Finished(T_INIT, State_EVENT_LOWBATTERY)` branch
for that case is commented out in `bt_state_run.c`, so the tag records the
condition but does not stop on it — worth knowing before reading a run that
ended near the floor.

### The first attach to a sleeping BitTag fails

With the tag asleep (~0.12 uA), the first monitor attach fails with
`Monitor attach failed: initial DEMCR read failed`. **That failed attach wakes
the part**, which then sits at ~376 uA, and the next attach succeeds. So issue
every host command **twice** against a sleeping BitTag. The first wakes it and
reports failure; the second does the work.

- **`tag_lifecycle_check.py` cannot drive a BitTag** until it retries the
  attach ([TODO](../TODO.md)). It fails at `[1/5] reset to idle` and leaves the
  tag awake, so run Phase A by hand.
- **A tag found at ~376 uA has probably just had a failed attach**, not a sleep
  fault. Reset it, let it settle, and measure again: a real failure to sleep
  survives that, and this does not.

Everything else about the rig -- the Joulescope server, detaching qtmonitor,
`mode=UR`, measuring after an attach -- is in the shared
[procedure](../../../../docs/bench/power-testing.md). It applies unchanged.

## 3. Qualify the released image, not a rebuild

Per [the release procedure](../../../../docs/release/release-procedure.md),
the image measured must be the image that ships.

```sh
embedded/tools/flash_release.py "$RELEASE/BitTag" \
    --label "$BOARD" --json "$OUT/flash.json"
```

The release **directory** is the argument, not the image: the tool verifies
`BitTag.bin` against the manifest beside it before programming, and cannot do
that if handed the bare image. `--json` takes a file because the programmer
writes to stdout too.

Record what was actually flashed, from the manifest rather than from anyone's
notes:

```sh
cp "$RELEASE/BitTag/BitTag-build-manifest.json" "$OUT/"
python3 - "$RELEASE/BitTag/BitTag-build-manifest.json" <<'EOF'
import json, sys
m = json.load(open(sys.argv[1]))
print("release :", m["source"]["describe"], "" if not m["source"]["dirty"] else "(DIRTY)")
print("commit  :", m["source"]["commit_short"])
print("bin     :", m["artifacts"]["BitTag.bin"]["sha256"])
print("toolchain:", m["tools"]["arm_gcc"]["version"])
EOF
```

A manifest reporting `dirty` is not a release and must not be qualified: see
step 1 of the release procedure. A toolchain version other than the pinned one
means the image is not the one CI published.

## 4. Configuration templates

Two configs, in `embedded/tools/power-configs/`:

- `bittag-bitpersec.json` — `BITTAG_BITPERSEC`, one record per minute. **Use
  this for the qualification.** It exercises the same once-a-minute wake as
  every other format while producing a record every minute, so a short run
  still yields a downloadable table to check.
- `bittag-default.json` — `BITTAG_BITSPERFIVEMIN`, the firmware default. One
  record per 35 minutes; used only in Phase B2 to confirm the format does not
  change the current.

**`active_interval` is required, and its absence is silent.** A config that
omits it programs `end_epoch = 0`, which is already in the past, so the run
ends one second after it starts: the marker log reads `CONFIGURED`, `RUNNING`,
`FINISHED reason=EVENT_ENDTIM` with the same timestamp, and no data is
recorded. The firmware's `defaultConfig` sets `end_epoch = INT32_MAX`, but a
config supplied to `tag-start` replaces the stored one rather than merging
unless `--merge` is passed. Both templates set
`active_interval.end_epoch = 2147483647`. This cost a run on the first
execution and is invisible to JSON validation, which is why it is written down
here.

**The tag type is `BITTAG_LE`, not `BITTAG`.** `inc/config.h` defines
`TAG_TYPE BITTAG_LE` for this target, and `tag-info` reports it. `BITTAG` is a
different enumerator that a config will still parse as, so the mistake is not
caught by validating the JSON -- only by comparing against what the tag
reports. Check `tag_type` in `tag-info` against the config before programming.

Both carry the firmware's default ADXL362 settings (R4G, S50, AAquarter,
0.35 g activity and inactivity, 0.24 s inactivity). The accelerometer config is
what sets the running floor, so **do not vary it and the format in the same
run**.

## 5. Phase A — resting states

The question: do `IDLE` and `FINISHED` both reach the Shutdown floor, after an
attach, repeatably? Until the life-cycle check retries its attach (§2), take
these four points by hand with `tag-reset`, `tag-start`, `tag-stop` and
`joulescope_measure.py`, retrying each attach. The command below is what the
check will run once it can:

```sh
embedded/tools/tag_lifecycle_check.py \
    --config "$CFG" \
    --run-duration 1200 --rest-duration 120 --settle 75 \
    --idle-max-ua 5 --use-server --verbose
```

`--rest-duration 120` rather than the 30 s default: a resting L432 draws
sub-microamp, and a longer window is needed before the charge integration is
meaningful at that level.

This reports four points — `idle_prepared`, `running`, `stopped`
(`FINISHED`), `idle_after_cycle`. Then repeat the idle measurement alone four
times, as `tag_release_check.py` does, because the fault class is
layout-sensitive and one reading is not a verdict:

```sh
for i in 1 2 3 4; do
  build-host/bin/tag-reset --set-rtc
  sleep 16
  embedded/tools/joulescope_measure.py --use-server --duration 120 --window 0.5 \
      | tee "$OUT/idle$i.log"
done
```

## 6. Phase B — running current

### B1, the qualification point

`BITTAG_BITPERSEC`, tag undisturbed, **1200 s** (twenty wakes). Twenty minutes
because the alignment error is 1/N and the agreement band below is 5%: a 600 s
window carries a 10% alignment error on its own and would fail a 5% band on
perfectly good hardware.

```sh
embedded/tools/power_experiment.py \
    --config "$CFG" \
    --duration 1200 --settle 75 --running-timeout 120 \
    --label bittag-bitpersec --use-server \
    --output "$OUT/running.csv" --keep-download "$OUT/download"
```

Run it **twice** and require the two to agree within 5%, which the 1200 s
window makes achievable. One window can hide both a missed wake and a tag that
never slept between them.

### B2, format independence

Repeat once with `CFG=embedded/tools/power-configs/bittag-default.json`. The prediction is that the current is
the same within measurement error, because the minute alarm does not change —
only how many minutes of bits go into a record. **If it differs, that is a
finding**: it would mean the record write, not the wake, dominates.

Note the 35-minute record period. `Running()` writes only when
`timestamp == lastwrite + sample_period`, and nothing in BitTag's `datalog.c`
flushes a partial word at stop, so a 1200 s run of this format should produce
**no stored record at all** and the download check should report an empty or
absent table. That is a prediction from the write condition, not something
this bench has observed — if a partial record does appear, the flush path is
not what it looks like and that is worth knowing. Use `--duration 2200` when a
record is actually wanted.

## 7. Phase C — functional, and the download

Power says nothing about whether the tag works.

**Reset to `IDLE` first.** From `FINISHED` the test fails with `SetRtc failed:
Monitor request not permitted in current tag state`; it sets the clock, which
that state does not permit. The download must therefore come first, since the
reset erases.

```sh
build-host/bin/tag-reset --set-rtc   # FINISHED -> IDLE; erases, so download first
build-host/bin/tag-test              # expect RUN_ALL -> ALL_PASSED
build-host/bin/tag-info              # record UUID, git hash, state
```

For the B1 database, confirm the activity table exists, its timestamps advance
monotonically, and the bit pattern is **all-zero or near it** for an
undisturbed tag. A quiescent tag reporting continuous activity means the
activity threshold or the `AWAKE` wiring is wrong, and would also explain an
unexpectedly high running current — check this before investigating power.

## 8. Phase D — activity sensitivity (optional, do last)

Deliberately agitate the tag for a 1200 s run and compare against B1. This is
not a gate; it bounds how far a field tag can sit above the quiescent floor,
which is the number a deployment estimate actually needs. Record how it was
agitated, because the measurement is not otherwise reproducible.

## 9. Pass/fail

The baseline is the first qualification: resting **0.1220 uA**, running
**0.5082 uA**, at 2.4960 V on `fw-v0.5`
([results](power-results.md)). It is one board on one day, not a spread, so the
gates below are sanity bounds rather than regression bounds. A run-current
bound waits for a second session ([TODO](../TODO.md)).

| Point | Gate | Rationale |
| --- | --- | --- |
| supply, every measurement | 2.45-2.55 V | no regulator; a figure at another voltage is not comparable |
| `IDLE` | ≤ 5 µA | "did it sleep" bound; CompassTag on the same `pwr-l432.c` reaches 0.38 µA |
| `FINISHED` | ≤ 5 µA, and within 20% of `IDLE` | same code path; a difference is a finding |
| `IDLE` repeatability | 4 of 4 trials below the bound | the fault class is layout-sensitive |
| `RUNNING` | recorded, not gated; two runs within 5% | one session is not a spread |
| state during the run | `RUNNING`, confirmed, not `CONFIGURED` | hard; see §1 |
| `tag-test` | `ALL_PASSED` | hard |
| download | table present, timestamps monotonic | hard, for B1 |

## 10. What this plan does not cover

- **Scheduling and hibernation.** `active_interval` and `hibernate` are in the
  config and untested here. PresTag's plan §1.6 has the pattern.
- **The never-attached cold baseline.** Every tool here attaches over SWD. A
  true cold number needs all power removed and no probe ever connected.
- **Board-to-board spread.** One board, one UUID. Record the UUID; do not
  generalise.
- **`tag_release_check.py` integration.** Its defaults are IMUTag's -- the
  config, the 60 s run and the sample-count check all assume IMUTag. Adding
  BitTag means parameterising the run duration and the resting-window length
  ([TODO](../TODO.md)).
- **Any supply but 2.5 V.** There is no regulator, so nothing here transfers to
  another rail voltage, and no scaling recovers it ([TODO](../TODO.md)).
- **Low-battery behaviour.** The stop-on-low-battery branch is commented out,
  so what the tag does as a cell collapses is untested and out of scope.

## 11. Recording results

Append each session to [`power-results.md`](power-results.md), recording what
the shared
[recording checklist](../../../../docs/bench/power-testing.md#7-recording-a-session)
asks for. Read the release tag, commit and `BitTag.bin` SHA-256 from the
manifest you copied in §3. `$OUT` holds the session's own evidence:
`flash.json`, the copied build manifest, `idle1..4.log`, `running.csv` and
`download/`.

**The result also goes onto the release page**, per *Publishing the
qualification* in
[the release procedure](../../../../docs/release/release-procedure.md). Fill in
the BitTag row of the release body's qualification table, state the supply
voltage, and attach `$OUT` as the evidence asset. A qualification that stays on
the bench cannot be acted on by whoever decides what to flash.
