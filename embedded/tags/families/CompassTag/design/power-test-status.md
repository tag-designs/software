# CompassTag Power Testing — Live Status

**Overwritten by whichever session is driving the rig. Read this first on
connect; do not accumulate history here — that belongs in
[`power-test-results.md`](power-test-results.md).**

Updated: **2026-09-22 ~19:30**

## Current objective

**Fix verified on one board (`CompassTagAT25Breakout`); not yet confirmed on
the plain `CompassTag`/`CompassTagAT25` targets that reproduced the pre-fix
fault.** That's the next session's first item — see "Outstanding" below.

## State right now

| | |
| --- | --- |
| worktree | clean, on `main` at `ef6033d` (fix `3ca3f99` + tooling fix `ef6033d` both committed) |
| tag firmware | `CompassTagAT25Breakout` at `3ca3f99`, clean rebuild (no diagnostic instrumentation) |
| tag state | IDLE, RTC set, measuring ~0.38 µA |
| instrument | `joulescope_server.py` **stopped** at end of session, DUT left powered |
| in flight | nothing |

## Done

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
  Numbers in [`power-test-results.md`](power-test-results.md).
- **Also fixed along the way**: `tag_lifecycle_check.py --use-server` was
  silently a no-op (Python late-binding default-argument bug) — `ef6033d`.
- Two false leads chased and ruled out before the real cause (both documented
  in [[compasstag-standby-decline-idle-current]] for anyone who reopens this):
  a floating `WKUP1`/accelerometer-wake pin, and a genuine but unrelated
  `isMonitorEnabled()`/`MONCONNECTED` latch bug that a clean `MONITORSTOP`
  measurably does not fix (ruled out empirically, not just by reading code).

## Outstanding

- **Reflash and re-measure `CompassTag` (MX25R) and `CompassTagAT25`
  (non-breakout) with the fix.** Both reproduced the pre-fix fault on
  separate physical boards this session; neither was re-measured after the
  fix. They share the fixed file (`pwr-l432.c`) with no override, so the fix
  should apply identically — confirm it on hardware before calling this
  closed across the family.
- A genuine cold power-cycle re-confirmation post-fix (VBAT/supply fully
  removed, not just SWD-disconnected) has not been done. The 376 nA figure in
  the report is the *pre-fix* cold measurement, used as the target.
- Two other CompassTag findings from this same session remain open and
  unrelated to this fix:
  - The ABORTED-on-first-boot fix (`379e3f1`) is committed but was never
    verified on real hardware (see [[compasstag-lse-bypass-fix]]).
  - `isMonitorEnabled()`'s `MONCONNECTED` latch bug and the host's blind
    `MONITORSTOP` success path (plan §4) are both real, both unrelated to
    this fix, and both still open.
