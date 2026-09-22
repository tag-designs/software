# CompassTag Power Test Plan — Standby-After-Attach Regression

Hardware-in-the-loop plan for the CompassTag family (`CompassTag`,
`CompassTagAT25`, `CompassTagAT25Breakout`) on STM32L432, wired to a
Joulescope. Scope is narrower than a full power/schedule campaign such as
[PresTag's](../../PresTag/design/power-test-plan.md): this plan exists to
verify one specific fix and to leave behind a repeatable regression check for
the fault class it closes, not to characterize sample-period sweeps,
scheduling, or hibernation, none of which changed here.

## 1. What was actually broken

`tagPowerEnterTerminalSleep()` (`common/core/src/pwr-l432.c`) is the shared
L432 terminal-sleep entry point. Commit `7ea0a86` ("Optimize L432 tag power
states", 2026-08-18) replaced its debugger-aware `DBGMCU->CR` handling with an
unconditional `DBGMCU->CR = 0`, dropping the `tagPowerDebuggerAttached()`
check that used to gate it.

`DHCSR.C_DEBUGEN` is set by the debug probe's own SWD protocol the moment it
attaches — not something this firmware's monitor code sets, and not something
`monitorStopI()`'s teardown clears (it only owns `DEMCR` bits). It can stay set
long after a clean, successful monitor detach. Telling `DBGMCU` not to retain
debug clocks through Standby while `C_DEBUGEN` is still set left the part
unable to reach genuine Standby current afterward.

**Measured effect, before the fix, on a CompassTagAT25Breakout board:**

| Condition | Current |
| --- | --- |
| Power-up cold, monitor never attached | **376 nA** |
| Any monitor attach + clean detach (even a single `tag-test`) | **~365 µA**, repeatable, does not self-clear |

That is roughly **1000×**. Every terminal state (`IDLE`, `FINISHED`,
`ABORTED`) is affected identically, since they all route through the same
`tagPowerEnterTerminalSleep()`. It is not visible on a never-attached,
field-deployed tag; it is very visible on any bench unit that has ever seen a
debugger, which is every unit under active development or bring-up — which is
why it looked, at first, like a hardware Standby-decline erratum (the same
class of fault documented for STM32U375 in `embedded/tags/design/open-issues.md`
and `AGENTS.md`) rather than a one-line regression in disconnect handling.

**The fix** (commit `3ca3f99`): restore `tagPowerDebuggerAttached()` and the
original two-way `DBGMCU->CR` handling — full clock gating when no debugger has
ever attached, debug-clock retention when one has.

## 2. Rig

- Board: CompassTagAT25Breakout, chosen because it has Joulescope current-sense
  connected and (separately) PA10/PA11/PA12 broken out for future
  logic-level diagnostics — unused by this plan's procedure, but see §6.
- Probe: ST-Link, `tag-*` CLI tools in `build-host/bin`.
- Joulescope: use `joulescope_server.py` + `--use-server`, not the
  `joulescope-js220` MCP server (holds the device for the session's lifetime;
  see [[joulescope-rig-discipline]]) and not the desktop app (must be detached
  for either path to open the device). Kill any stray `joulescope-mcp` process
  first — it will hold the instrument exclusively and every open attempt will
  fail with `jsdrv_open timed out` without explaining why.
- `tag_lifecycle_check.py --use-server` needed a one-line fix (commit
  `ef6033d`) before this plan could be run at all: `--use-server` was silently
  a no-op due to a Python late-binding default-argument bug, so every prior
  attempt to use it fell through to opening the Joulescope directly and
  collided with the server. Confirm the fix is present before relying on the
  flag.
- Flashing needs `mode=UR` (connect under reset) on this rig; a plain
  `port=SWD` connect fails with `Unable to get core ID` even on a healthy,
  running target. See [[joulescope-rig-discipline]] and the L432 rig traps
  noted in [[prestag-power-test-state]] — the same family of traps applies
  here.

## 3. What to measure

Unlike PresTag's plan, there is no sample-period sweep or schedule phase here
— CompassTag's `RUN_ALL`/config surface wasn't touched. The check is: does
every terminal-sleep state reach the never-attached baseline, **specifically
after the tag has been attached and cleanly detached**, since that is exactly
the condition the bug needs to reproduce and a cold, never-touched boot cannot
exercise it.

### Phase A — full life-cycle sweep

```sh
python3 embedded/tools/tag_lifecycle_check.py \
    --bin-dir build-host/bin \
    --measure-python <path-to-python-with-pyjoulescope_driver> \
    --use-server \
    --run-duration 20 --rest-duration 30 --verbose
```

This alone reproduces the fault condition: it attaches (`tag-reset`) before
every rest-state measurement, so every resting number it reports is a
post-attach number, not a cold-boot number. Four points, one pass:

| state | what | pre-fix (measured) | post-fix (measured) | gate |
| --- | --- | --- | --- | --- |
| `idle_prepared` | IDLE, clock set, right after `tag-reset` | ~362 µA | 0.38 µA | ≤ 5 µA |
| `running` | RUNNING, collecting | 338 µA | 173 µA | not gated (active state; see §5) |
| `stopped` | FINISHED, data not yet read | ~366 µA | 0.38 µA | ≤ 5 µA |
| `idle_after_cycle` | IDLE, after a full reset→run→stop→download→reset cycle | ~367 µA | 0.38 µA | ≤ 5 µA |

The 5 µA gate is deliberately loose relative to the measured 0.38 µA — it is
a "did it actually sleep" sanity bound, not a tight regression bound. Tighten
it once more sessions have established the board-to-board spread; see §5.

### Phase B — repeated attach patterns

Phase A's `tag-reset` is one specific attach pattern. Confirm the fix holds
across others, since the bug's mechanism (a debug-domain register outliving
the software session) does not obviously depend on which monitor request
triggered the attach:

```sh
build-host/bin/tag-reset --set-rtc
sleep 12   # let the tag settle and attempt sleep
<measure 15-20s>

build-host/bin/tag-test        # different attach: RUN_ALL, GetTagInfo, SetRtc
sleep 12
<measure 15-20s>
```

Both must land at the Phase A baseline. If either does not, the fix is
incomplete for that request path — do not assume Phase A's result generalizes
without checking.

### Phase C — regression sanity

The fix only touches `DBGMCU->CR` handling in the Standby-entry path; it does
not touch sensor, storage, or state-machine code. Confirm nothing else moved:

```sh
build-host/bin/tag-test        # expect RUN_ALL -> ALL_PASSED
```

## 4. What this plan does not cover

- **The never-attached, cold-power baseline (376 nA) is asserted from the
  operator's own bench measurement** (power-up with no debugger ever
  connected), not reproduced by this plan's procedure — every tool this plan
  uses attaches over SWD to drive the tag, which is exactly the condition
  under test. If that baseline needs re-confirming, it requires physically
  removing all power (including any coin-cell/battery path) and measuring
  before ever touching the board with a probe.
- Sample-period sweep, scheduled start/stop, hibernation: unchanged by this
  fix, out of scope. See PresTag's plan for the pattern if CompassTag ever
  needs the equivalent campaign.
- The separate, pre-existing `isMonitorEnabled()`/`MONCONNECTED` latch bug
  documented in `handlers.c` (STM32L4 has no `TAG_MONITOR_MAILBOX`) is real
  but was ruled out as the cause of *this* fault during investigation — a
  clean `MONITORSTOP` measurably does not fix the stuck-current symptom, only
  the `DBGMCU->CR` fix does. Left as-is; not part of this fix's scope.
- The host-side `TagMonitor::Call()` `MONITORSTOP` path reports success after
  a blind 5 ms sleep rather than polling for completion like every other
  operation, unlike here where the fix stayed tag-side by design (any change
  to shared host software needs a validation cycle across every tag). Noted
  as a candidate follow-up, not fixed here.

## 5. Pass/fail and baselining

First clean execution after the fix, on CompassTagAT25Breakout:

- **Every terminal state (`IDLE`, `FINISHED`, post-cycle `IDLE`) below 5 µA**,
  immediately following an attach+detach. Hard gate.
- **The never-attached baseline and the post-attach numbers agree within an
  order of magnitude** (376 nA vs. 380 nA measured — effectively identical;
  do not expect this exactly on every board, some debug-clock-retention
  residual is plausible when a debugger is *still* connected at the moment of
  sleep entry, as opposed to previously-but-not-currently attached).
- `running` is recorded, not gated — it is an active state and this fix does
  not target it.
- `tag-test` reports `ALL_PASSED`.

These numbers become the baseline for future CompassTag sessions. A resting
state drifting back toward the hundreds-of-µA range on a board that has been
attached is the specific regression this plan exists to catch — if it
reappears, suspect another unconditional `DBGMCU->CR` write, or a similar
debug-domain-register/software-session mismatch introduced elsewhere.

## 6. Available but unused: PA10/PA11/PA12 on the breakout board

These are wired to Joulescope digital inputs on this specific board, unused
by this fix's final form but potentially useful for a future investigation
that needs to correlate firmware state with the current trace without an SWD
read (which the [[compasstag-standby-decline-idle-current]] investigation
found unreliable for this purpose — a debugger connection is itself
intrusive to what it's trying to observe here). They are genuine GPIO pins
(`PAL_LINE(GPIOA, 10/11/12)`), not currently used by any CompassTag driver —
confirm against the board's pin table before reusing them for something else.

## 7. Recording results

Results go in [`power-test-results.md`](power-test-results.md) (append-only
log) and [`power-test-report.md`](power-test-report.md) (one session block
per run). Record for every session:

- git hash, whether the tree was dirty, and the exact target built
  (`CompassTag` / `CompassTagAT25` / `CompassTagAT25Breakout` — they share
  `pwr-l432.c` but are otherwise distinct firmware images);
- board UUID (`tag-test` reports it) — CompassTag boards in this project have
  turned out to need per-board attention (see [[compasstag-flash-target-mismatch]]);
  do not assume a result from one board generalizes without checking against
  its own UUID;
- the interpreter used for the Joulescope, and confirmation the server was
  used, not the desktop app or the MCP server;
- confirmation qtmonitor was detached.
