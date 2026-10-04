---
type: procedure
status: current
summary: CompassTag power test plan -- qualifying a CompassTagAT25 release image for idle, running and finished current, with the Standby-after-attach regression as a standing gate; calibration must survive the flash.
---

# CompassTag Power Test Plan

This plan covers the CompassTag family (`CompassTag`, `CompassTagAT25`,
`CompassTagAT25Breakout`) on the STM32L432. It does two jobs:

1. **Qualify a release image** of `CompassTagAT25` for the three currents that
   set a deployment -- `IDLE`, `RUNNING` and `FINISHED`.
2. **Hold the Standby-after-attach gate**: after a debugger has attached and
   cleanly detached, every terminal state must still reach the never-attached
   Standby floor. That is the fault class
   [the Standby-after-attach investigation](investigations/2026-09-compasstag-standby-after-attach.md)
   closed, and it needs no extra step here, because every tool attaches and so
   every resting figure below is already a post-attach figure.

Where it stands: on a production `CompassTagAT25`, `IDLE` measured **0.23 uA**
and `RUNNING` **1.95 uA** at the 30 s compass interval; on
`CompassTagAT25Breakout` every resting state measured **0.38 uA** after an
attach ([results](power-results.md)). `FINISHED` has never been measured on
either, and neither figure came from a release image.

The rig, the Joulescope server, flashing, supply voltage and what to record are
in the shared
[power testing procedure](../../../../../docs/bench/power-testing.md). Open work
is in [`../TODO.md`](../TODO.md). Measurements go in
[`power-results.md`](power-results.md).

## 0. Inputs

Set these once per session; the commands below then work unaltered. Nothing in
this document names a release.

| Input | Shell name | How to obtain it |
| --- | --- | --- |
| Firmware release | `$RELEASE` | The unpacked release's `firmware/` directory |
| Board label | `$BOARD` | The unit's physical label; the UUID if it has none |
| Results directory | `$OUT` | A fresh directory per session |

```sh
RELEASE=<unpacked-release>/firmware        # contains CompassTagAT25/, ...
BOARD=<physical-label>
OUT=release-checks/compasstagat25-$(date +%Y%m%d-%H%M%S)
CFG=embedded/proto-c/compasstag-proto-c/default-config.json
mkdir -p "$OUT"
```

`CFG` is the shipped default rather than a `power-configs/` entry, because
CompassTag has nothing to sweep: the sample period is the compile-time
constant `COMPASS_SAMPLE_PERIOD_S`, not a configuration field. The default
already sets `active_interval` to `0 .. INT32_MAX`, which matters — a config
that omits it programs `end_epoch = 0` and the run ends one second after it
starts, which is how a BitTag run was lost
([BitTag results](../../../BitTag/design/power-results.md)).

## 1. What the firmware does

| State | Sleep | Path |
| --- | --- | --- |
| `IDLE`, `FINISHED`, `ABORTED` | Standby | `tagPowerEnterTerminalSleep()` in `common/core/src/pwr-l432.c` |
| `RUNNING` | Standby between samples | `Running()` returns `STANDBY` |

Every terminal state goes through the same `tagPowerEnterTerminalSleep()`, so a
fault there shows in all of them at once — and `IDLE` and `FINISHED` should
therefore **measure the same**. A difference between them is a finding, not
noise. Since `42a4a618` that function clears `DBGMCU->CR` once
`isMonitorEnabled()` reports no monitor session, so debug clocks are not kept
running through Standby after a stale attach.

**The sample period is 30 s** (`COMPASS_SAMPLE_PERIOD_S` in
`inc/datalog.h`), and samples are downloaded in blocks of three. The tag wakes
every 30 s, takes a magnetometer and accelerometer sample, and returns to
Standby.

### Sizing the running window

A run of N ticks contains N or N+1 wakes depending on where the window opens,
so the alignment error alone is **1/N**. At 30 s per tick:

| Window | Ticks | Alignment error |
| ---: | ---: | ---: |
| 180 s | 6 | 17% |
| 600 s | 20 | 5% |
| 900 s | 30 | 3.3% |

**Use 900 s.** The existing production figure (1.9538 µA, 2026-09-24) came
from a 180 s window, so it carries a 17% alignment error of its own and should
not be compared to a new measurement more tightly than that.

## 2. Before you flash

### Which board

- **A production `CompassTagAT25`** is what a release qualification must be
  taken on, because that is what flies. It carries calibration, so the backup
  below is mandatory.
- **`CompassTagAT25Breakout`** has Joulescope current sensing wired in and
  PA10/PA11/PA12 broken out to Joulescope digital inputs (§5), which makes it
  the better board for chasing a fault. It is **not** a substitute for
  qualifying the production unit: its `MAG_PWR` genuinely cuts I/O power, so
  it does not need the magnetometer pull-down fix the production board does,
  and its idle figures are not comparable.

### Calibration — never mass erase, and check where the region actually is

Production CompassTag boards carry user calibration in `.calibration`, 2016
bytes. Recovering it means re-running `tag-cal`/`qtcalibrate` on a unit that
may be calibrated against a real deployment.

**Never mass erase.** `flash_release.py` does a normal download —
`STM32_Programmer_CLI ... -d <image> -g 0x08000000`, with no `-e all` — and the
`.calibration` section is `NOBITS` with a zero file size, so the image contains
no bytes for it and programming does not write that region. Verified against
the released ELF, not assumed.

> **The region is not at a fixed address on this part, and that is the real
> hazard.** The L432 linker places it as `.calibration (NOLOAD): ALIGN(2048)`
> immediately after the code, so **it moves when the image grows**. The
> STM32U375 script pins both bounds with eleven `ASSERT`s precisely "so that
> provisioned configuration and calibration survive a firmware update"; the
> L432 script has one `ASSERT`, and it is about `.tag_identity`.
>
> It has already moved. At `4160d1e` the calibration sat at `0x0800a800`; in
> `fw-v0.5` the ELF puts `__calibration_start__` at **`0x0800b000`**, one page
> higher, with the loaded image ending at `0x0800aebc` — **324 bytes of
> headroom**. A board calibrated under the older image therefore has its
> calibration bytes at an address the new firmware does not read. Nothing
> erased them; the firmware is simply looking somewhere else.
>
> **Resolve this before flashing a calibrated production unit.** Read the
> address out of the ELF being flashed and compare it against the one the
> board was calibrated under; if they differ, the calibration must be
> relocated or re-taken, and neither is part of a power measurement.

Read the address from the image rather than hardcoding it:

```sh
ELF="$RELEASE/CompassTagAT25/CompassTagAT25.elf"
CAL=$(arm-none-eabi-nm "$ELF" | awk '$3=="__calibration_start__"{print "0x"$1}')
echo "calibration at $CAL"
STM32_Programmer_CLI -c port=SWD mode=UR -u "$CAL" 2016 "$OUT/calibration-before.bin"
# ... flash ...
STM32_Programmer_CLI -c port=SWD mode=UR -u "$CAL" 2016 "$OUT/calibration-after.bin"
cmp "$OUT/calibration-before.bin" "$OUT/calibration-after.bin" && echo "calibration intact"
```

### Flash the right target for the board

`CompassTag` is built for the MX25R part and `CompassTagAT25` for an AT25XE
board. They share `pwr-l432.c` but are distinct images, and **nothing in the
tooling flags the mismatch**. `CompassTagAT25` firmware on Breakout hardware
runs and idles correctly but fails its self-test with `RTC_FAILED`, because the
I2C wiring differs ([results](power-results.md), 2026-09-22 ~19:55).

```sh
embedded/tools/flash_release.py "$RELEASE/CompassTagAT25" \
    --label "$BOARD" --json "$OUT/flash.json"
cp "$RELEASE/CompassTagAT25/CompassTagAT25-build-manifest.json" "$OUT/"
```

A manifest reporting `dirty` is not a release and must not be qualified.

### Supply

**There is no regulator.** Like BitTag and PresTag, the parts run straight off
a nominal **2.5 V** cell, so no voltage scaling applies and a figure holds only
at the voltage it was taken at — see shared procedure §5. Past sessions were
taken at ~2.485 V.

Set the supply to 2.5 V and confirm it on the Joulescope **before the tag is
connected**, then require 2.45-2.55 V on every measurement and record it. The
bench is shared with IMUTag work at 3.7 V, which is above the absolute maximum
for parts fed directly from the rail.

## 3. Phases

### Phase A — the three currents

`tag_lifecycle_check.py` walks idle, running, stopped and idle again, and
attaches before every resting measurement.

```sh
embedded/tools/tag_lifecycle_check.py \
    --config "$CFG" --run-duration 900 --rest-duration 120 --settle 60 \
    --idle-max-ua 1 --use-server --verbose
```

**`--idle-max-ua 1`, not 5.** The flag does two jobs: it is the bound a
resting state must come under, *and* the bound the running state must come
**over**, because a run drawing idle current collected nothing. On IMUTag those
are three orders of magnitude apart and any value between them works. On
CompassTag they are a factor of 8.5 apart and **both below 5 uA** — idle 0.23,
running 1.96 — so a 5 uA threshold declares the run asleep and fails it. The
2026-10-04 session did exactly that: the tool reported
`running: 1.96 uA, at or below the 5 uA sleep threshold -- the tag was not
collecting` in the same run whose download check passed with 30 rows, proving
it had collected. 1 uA sits above the floor and below the run.

Two further departures from the invocation this plan used to carry:

- **`--run-duration 900`, not 20.** A 20 s run at a 30 s sample period
  produces no sample at all, so the download check has nothing to look at and
  the running figure is one wake's worth of alignment noise.
- **`--rest-duration 120`.** The resting floor is sub-microamp; a 30 s window
  is not enough charge to integrate against.

If the tool fails at `[1/5]` with `Monitor attach failed: initial DEMCR read
failed`, it has hit the attach-from-sleep problem that currently stops it
driving a BitTag. **Whether CompassTag suffers it is not known** — earlier
sessions drove this tool successfully, so probably not. If it does, run the
phases by hand, issuing each command twice, and record that it does.

### Phase B — running current, measured properly

Phase A's running figure is corroborative. The qualification figure comes from
two independent windows that must agree:

```sh
embedded/tools/power_experiment.py \
    --config "$CFG" --duration 900 --settle 60 \
    --label compasstagat25 --use-server \
    --output "$OUT/running.csv" --keep-download "$OUT/download"
```

Run it **twice** and require the two within 5%, which a 900 s window supports.
Confirm the state is `RUNNING`, not `CONFIGURED`, before trusting either.

### Phase C — download and functional

The download is the evidence the run recorded anything.

```sh
build-host/bin/tag-dwnld -f sqlite -o "$OUT/download.db3"
build-host/bin/tag-reset --set-rtc    # FINISHED -> IDLE; erases, so download first
build-host/bin/tag-test               # expect RUN_ALL -> ALL_PASSED
```

`tag-test` sets the clock, which `FINISHED` does not permit, so it must run
from `IDLE` and therefore after the download.

Check the database: samples exactly **30 s** apart, magnetometer and
accelerometer values stable and non-saturated on a stationary tag, and
activity 0.0 throughout. Activity 0.0 is expected on a stationary bench tag and
is **not** a test of the activity encoding, which needs real motion.

### Phase D — other attach patterns

The Standby gate must hold for every attach pattern, not just `tag-reset`:

```sh
build-host/bin/tag-reset --set-rtc
sleep 12
embedded/tools/joulescope_measure.py --use-server --duration 120 --repeat 2

build-host/bin/tag-test        # a different attach: RUN_ALL, GetTagInfo, SetRtc
sleep 12
embedded/tools/joulescope_measure.py --use-server --duration 120 --repeat 2
```

Both must land at the Phase A resting floor.

## 4. Gates

Baseline, production `CompassTagAT25` UUID `203633324B425006004A005D` at
`4160d1e`, 2026-09-24: `IDLE` 0.2346 and 0.2317 µA, `RUNNING` 1.9538 µA over
180 s, self-test `ALL_PASSED`. `FINISHED` **was never measured** — this plan
adds it.

| Point | Gate | Rationale |
| --- | --- | --- |
| supply, every measurement | 2.45-2.55 V | no regulator, so a figure means nothing at another voltage |
| calibration address | the ELF's `__calibration_start__` matches the one the board was calibrated under | it is not pinned on L432 and has already moved once |
| calibration | byte-identical before and after flashing | hard; a lost calibration costs a recalibration against a real deployment |
| `IDLE` | ≤ 5 µA | a "did it sleep" bound, deliberately loose against a ~0.23 µA floor |
| `FINISHED` | ≤ 5 µA, and within 20% of `IDLE` | same code path; a difference is a finding |
| `idle_after_cycle` | ≤ 5 µA | the same state by a second history |
| Phase D, both patterns | at the Phase A floor | the Standby fix must hold for every attach pattern |
| `RUNNING` | two windows within 5% | recorded against the 1.95 µA baseline, not gated on it: that figure has a 17% alignment error |
| `tag-test` | `ALL_PASSED` | hard |
| download | samples exactly 30 s apart | hard |

A resting state drifting back toward hundreds of microamps on a board that has
been attached is the specific regression this plan catches. If it returns,
suspect a write to a debug-domain register that disagrees with the debug
session's real state.

## 5. Available but unused: PA10/PA11/PA12 on the breakout

These pins are wired to Joulescope digital inputs on the breakout board. They
are genuine GPIOs (`PAL_LINE(GPIOA, 10/11/12)`) that no CompassTag driver uses.
That makes them useful for correlating firmware state with the current trace
without an SWD read, which is itself intrusive here. Before reusing them,
confirm against the board's pin table. The
[debugging guide](../../../../../docs/bench/debugging-a-tag.md) lists the
artefacts GPIO markers have produced on this bench.

## 6. Not covered

- **The never-attached cold baseline.** Every tool attaches. Measuring it means
  removing all power, including any cell, before a probe ever touches the
  board. A BitTag session caught one only because the tag happened to be found
  asleep.
- **Sample-period sweep.** There is no sweep to run: the period is a
  compile-time constant.
- **Scheduled start and stop, and hibernation.** Exercised on real hardware in
  2026-09 but not part of this gate; see [`../TODO.md`](../TODO.md).
- **Board-to-board spread.** CompassTag boards have needed per-board attention.
  Record the UUID and do not generalise from one unit.
- **Relocating calibration across a firmware update.** This plan refuses to
  flash a calibrated unit whose calibration address has moved; making that
  survivable — pinning the region as the U375 script does, or migrating the
  bytes — is firmware work, not a power measurement.
- **Whether `debug_log` belongs in a shipped image.** `CompassTagAT25`'s
  `project.mk` lists it in `TAG_MODULES`, while the debug module is kept out of
  shipped IMUTag images because instrumentation in the idle path has changed
  the fault being measured. Whether it costs anything here is unmeasured; it is
  a question for the first session that has spare time, not a claim that it
  does.

## 7. Recording results

Append each session to [`power-results.md`](power-results.md), recording what
the shared
[recording checklist](../../../../../docs/bench/power-testing.md#7-recording-a-session)
asks for. State the exact target built, and whether calibration was preserved.

A release qualification also goes onto the release page: see *Publishing the
qualification* in
[the release procedure](../../../../../docs/release/release-procedure.md).
