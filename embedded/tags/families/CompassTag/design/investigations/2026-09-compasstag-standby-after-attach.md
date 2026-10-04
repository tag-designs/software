---
type: investigation
status: closed
summary: Why every L432 terminal state drew ~365 uA after a debugger attach (an unconditional DBGMCU->CR = 0 from 7ea0a86) and the fix restoring debugger-aware handling (3ca3f99).
---

# CompassTag Standby-After-Attach Regression (2026-09)

Cut verbatim from section 1 and section 2 of
[`../power-test-plan.md`](../power-test-plan.md) and from the closing handoff
in [`../power-test-status.md`](../power-test-status.md). The regression entered
on 2026-08-18 (`7ea0a86`) and was found and fixed on 2026-09-22 (`3ca3f99`),
verified on `CompassTagAT25Breakout`; the measurements are in
[`../power-test-results.md`](../power-test-results.md) and the session
write-up in [`../power-test-report.md`](../power-test-report.md). Links of the
form `[[name]]` refer to agent memory notes that are not in this repository.

## What was actually broken

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

## A tooling fix needed first

- `tag_lifecycle_check.py --use-server` needed a one-line fix (commit
  `ef6033d`) before this plan could be run at all: `--use-server` was silently
  a no-op due to a Python late-binding default-argument bug, so every prior
  attempt to use it fell through to opening the Joulescope directly and
  collided with the server. Confirm the fix is present before relying on the
  flag.

## Handoff at the close of the investigation

From [`../power-test-status.md`](../power-test-status.md), as last written.

Updated: **2026-09-22 ~19:30**

### Current objective

**Fix verified on one board (`CompassTagAT25Breakout`); not yet confirmed on
the plain `CompassTag`/`CompassTagAT25` targets that reproduced the pre-fix
fault.** That's the next session's first item — see "Outstanding" below.

### State right now

| | |
| --- | --- |
| worktree | clean, on `main` at `ef6033d` (fix `3ca3f99` + tooling fix `ef6033d` both committed) |
| tag firmware | `CompassTagAT25Breakout` at `3ca3f99`, clean rebuild (no diagnostic instrumentation) |
| tag state | IDLE, RTC set, measuring ~0.38 µA |
| instrument | `joulescope_server.py` **stopped** at end of session, DUT left powered |
| in flight | nothing |

### Done

- **Root cause found and fixed**: `7ea0a86` ("Optimize L432 tag power
  states", 2026-08-18) replaced conditional `DBGMCU->CR` handling in
  `tagPowerEnterTerminalSleep()` with an unconditional `DBGMCU->CR = 0`,
  dropping the `tagPowerDebuggerAttached()` check. `DHCSR.C_DEBUGEN` — set by
  the probe's own SWD protocol on attach, not cleared by this firmware's
  monitor teardown — could stay set after a clean detach, and the
  unconditional write then told `DBGMCU` not to retain debug clocks through
  Standby while `C_DEBUGEN` was still set: an inconsistent state that left
  the part unable to reach genuine Standby current. Fixed at `3ca3f99` by
  restoring the check.
- **Measured**: pre-fix, never-attached cold boot 376 nA vs. any attach+detach
  ~365 µA (operator-measured, repeatable). Post-fix, both `tag-reset` and
  `tag-test` attach patterns land at ~0.378 µA — matching the cold baseline.
  Full life-cycle sweep (idle/running/FINISHED/idle-after-cycle) passes.
  Numbers in [`power-test-results.md`](../power-test-results.md).
- **Also fixed along the way**: `tag_lifecycle_check.py --use-server` was
  silently a no-op (Python late-binding default-argument bug) — `ef6033d`.
- Two false leads chased and ruled out before the real cause (both documented
  in [[compasstag-standby-decline-idle-current]] for anyone who reopens this):
  a floating `WKUP1`/accelerometer-wake pin, and a genuine but unrelated
  `isMonitorEnabled()`/`MONCONNECTED` latch bug that a clean `MONITORSTOP`
  measurably does not fix (ruled out empirically, not just by reading code).
