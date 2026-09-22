# CompassTag Power Test Report — Standby-After-Attach Regression

Results for the procedure in [`power-test-plan.md`](power-test-plan.md). One
section per session; keep old sessions rather than overwriting them.

**Status: fix verified 2026-09-22, on `CompassTagAT25Breakout` only.** The
`CompassTag` and `CompassTagAT25` targets share the fixed file
(`common/core/src/pwr-l432.c`) and were confirmed to reproduce the *pre-fix*
fault on separate physical boards, but were not reflashed with the fix and
re-measured in this session — see "Not yet run" below.

---

## Session 2026-09-22

### Provenance

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

### Phase A — life-cycle sweep

Gate: every terminal state below 5 µA, post-attach.

| state | pre-fix | post-fix | gate | verdict |
| --- | --- | --- | --- | --- |
| idle_prepared | 362.40 µA | 0.38 µA | ≤ 5 µA | **pass** |
| running | 338.11 µA | 173.36 µA | not gated | recorded |
| stopped (FINISHED) | 365.91 µA | 0.38 µA | ≤ 5 µA | **pass** |
| idle_after_cycle | 366.88 µA | 0.38 µA | ≤ 5 µA | **pass** |

Full log entries: [`power-test-results.md`](power-test-results.md).

### Phase B — repeated attach patterns

| pattern | result | gate | verdict |
| --- | --- | --- | --- |
| `tag-reset --set-rtc` | 0.3786 µA | matches never-attached baseline | **pass** |
| `tag-test` (RUN_ALL + GetTagInfo + SetRtc) | 0.3789 µA | matches never-attached baseline | **pass** |

Never-attached baseline (operator-measured, cold power-up, no probe ever
connected): **376 nA**. Both attach patterns land within ~1% of it.

### Phase C — regression sanity

`tag-test` reported `Test Result: ALL_PASSED` (RTC, magnetometer, accelerometer,
external flash) both before and after the fix — the change is confined to the
`DBGMCU->CR` path in `tagPowerEnterTerminalSleep()` and touches nothing else.

### Gate summary

All gates in §5 of the plan met on `CompassTagAT25Breakout`. The fix restores
the never-attached idle-current baseline after every attach pattern tested.

### Not yet run

- **`CompassTag` (plain, MX25R) and `CompassTagAT25` (non-breakout) were not
  reflashed with the fix and re-measured.** Both were confirmed to reproduce
  the *pre-fix* fault (see results log), and both share the exact fixed file
  with no target-specific override, so the fix applies identically by
  construction — but "by construction" is not a hardware measurement. Treat
  this as the next session's first item before calling the fix qualified
  across the whole family.
- No genuine power-cycle (full VBAT/supply removal) re-confirmation was done
  after the fix — the 376 nA figure cited throughout is the operator's
  pre-fix cold-boot measurement, used as the target the fix needed to
  reproduce under attach, not re-measured cold post-fix.
- The pre-existing `isMonitorEnabled()`/`MONCONNECTED` latch bug in
  `handlers.c` (see plan §4) is unrelated to this fix and remains open.
- The host-side `TagMonitor::Call()` blind-success `MONITORSTOP` path (plan
  §4) is unrelated to this fix and remains open; flagged as a candidate
  follow-up, deliberately not touched here per the decision to keep this fix
  scoped to tag-side code.
