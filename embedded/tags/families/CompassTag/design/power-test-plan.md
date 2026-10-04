---
type: procedure
status: current
summary: CompassTag power check -- every terminal state must reach the Standby floor after a debugger attach and detach; phases, gates and the board to run it on.
---

# CompassTag Power Test Plan — Standby After Attach

This plan covers the CompassTag family (`CompassTag`, `CompassTagAT25`,
`CompassTagAT25Breakout`) on the STM32L432. Its gate: **after a debugger has
attached and cleanly detached, every terminal state must reach the
never-attached Standby floor.** The floor is sub-microamp. On
`CompassTagAT25Breakout` every resting state measured 0.38 uA after an attach;
on a production `CompassTagAT25`, IDLE measured 0.23 uA and RUNNING 1.95 uA at
the 30 s compass interval
([results](power-results.md)). The plan is a regression check for the fault
class
[the Standby-after-attach investigation](investigations/2026-09-compasstag-standby-after-attach.md)
closed. It does not characterise sample-period sweeps, scheduling or
hibernation.

The rig, the Joulescope server, flashing, and what to record are in the shared
[power testing procedure](../../../../../docs/bench/power-testing.md). Open work
is in [`../TODO.md`](../TODO.md).

## 1. What the firmware does

| State | Sleep | Path |
| --- | --- | --- |
| `IDLE`, `FINISHED`, `ABORTED` | Standby | `tagPowerEnterTerminalSleep()` in `common/core/src/pwr-l432.c` |
| `RUNNING` | Standby between samples | `Running()` returns `STANDBY` |

Every terminal state goes through the same `tagPowerEnterTerminalSleep()`, so a
fault there shows in all of them at once. Since `42a4a618`, that function
clears `DBGMCU->CR` unconditionally once `isMonitorEnabled()` reports no
monitor session, so debug clocks are not kept running through Standby after a
stale attach.

The fault this plan guards against does not show on a never-attached tag. It
shows on any bench unit that has seen a debugger: the debug-domain state
outlives the software session, and the part never reaches genuine Standby
current. That is why every measurement here is taken **after** an attach.
Every tool used here attaches, so this needs no extra step. A cold-boot number
cannot exercise the fault.

## 2. Rig

- **Board: `CompassTagAT25Breakout`.** It has Joulescope current sensing wired
  in, and PA10/PA11/PA12 broken out to Joulescope digital inputs (§5).
- **Production boards** carry user calibration in the `.calibration` section.
  On the measured unit it was at `0x0800a800`, 2016 bytes. Back it up before
  flashing, and confirm afterwards that it is byte-identical; then no
  recalibration is needed.
- **Flash the right target for the board.** The three targets share
  `pwr-l432.c` but are distinct images. `CompassTagAT25` firmware on Breakout
  hardware runs, idles correctly, and fails its self-test with `RTC_FAILED`
  because the I2C wiring differs ([results](power-results.md), 2026-09-22
  ~19:55). Nothing in the tooling flags the mismatch.

## 3. Phases

### Phase A — life cycle

```sh
python3 embedded/tools/tag_lifecycle_check.py \
    --bin-dir build-host/bin \
    --measure-python <path-to-python-with-pyjoulescope_driver> \
    --use-server \
    --run-duration 20 --rest-duration 30 --verbose
```

It attaches (`tag-reset`) before every resting measurement, so each resting
figure is a post-attach figure. It reports `idle_prepared`, `running`,
`stopped` (`FINISHED`) and `idle_after_cycle`.

### Phase B — other attach patterns

Phase A's `tag-reset` is one attach pattern. The fault's mechanism does not
obviously depend on which monitor request caused the attach, so check another:

```sh
build-host/bin/tag-reset --set-rtc
sleep 12   # let the tag settle and sleep
embedded/tools/joulescope_measure.py --use-server --duration 20 --repeat 2

build-host/bin/tag-test        # a different attach: RUN_ALL, GetTagInfo, SetRtc
sleep 12
embedded/tools/joulescope_measure.py --use-server --duration 20 --repeat 2
```

Both must land at the Phase A floor. If one does not, the fix is incomplete
for that request path.

### Phase C — functional

```sh
build-host/bin/tag-test        # expect RUN_ALL -> ALL_PASSED
```

## 4. Gates

| Point | Gate | Rationale |
| --- | --- | --- |
| `idle_prepared`, `stopped`, `idle_after_cycle` | ≤ 5 uA | A "did it sleep" bound, deliberately loose against a sub-microamp floor. Tighten it once more boards have set the spread |
| Phase B, both patterns | at the Phase A floor | the fix must hold for every attach pattern |
| post-attach vs. never-attached | same order of magnitude | they measured 376 nA cold and ~0.38 uA after an attach. Some residual is plausible if a debugger is *still* connected at sleep entry |
| `running` | recorded, not gated | an active state |
| `tag-test` | `ALL_PASSED` | hard |

A resting state drifting back toward hundreds of microamps on a board that has
been attached is the specific regression this plan catches. If it comes back,
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

- **The never-attached cold baseline.** Every tool attaches. Measuring it
  means removing all power, including any cell, and measuring before a probe
  ever touches the board.
- **Sample-period sweep, scheduled start and stop, hibernation.** See the
  [PresTag plan](../../PresTag/design/power-test-plan.md) for that pattern.
- **Board-to-board spread.** CompassTag boards have needed per-board attention.
  Record the UUID, and do not generalise from one board.

## 7. Recording results

Append each session to [`power-results.md`](power-results.md), recording what
the shared
[recording checklist](../../../../../docs/bench/power-testing.md#7-recording-a-session)
asks for. State the exact target built, and whether calibration was preserved.
