# PresTag Power and Schedule Test Report

Results for the procedure in [`power-test-plan.md`](power-test-plan.md). One
section per session; keep old sessions rather than overwriting them, so a
regression can be bisected against a number someone actually took.

**Status: first session executed 2026-09-08 (partial).** Phase A1/A2, B2/B3/B7
and one download check were measured; the rest of the form is still blank.
Summary and interpretation are in the plan's §11.

---

## Session template

Copy this whole block for each run.

### Provenance

| Item | Value |
| --- | --- |
| Date (UTC) | 2026-09-08 |
| Operator | G. Brown / Claude (Claude Code) |
| git hash | `411b046` (+ uncommitted probe/tooling edits, see §11 of the plan) |
| **Tree dirty?** | yes — tooling fixes and probe instrumentation uncommitted |
| Target built | `PresTag` |
| `tag-info` firmware string | PresTagv4, Firmware version 1, githash 411b046 |
| Board / serial | |
| Supply | baseboard via Joulescope JS320, **2.485 V** |
| Joulescope interpreter | /home/geobrown/opt/joulescope-mcp/.venv/bin/python |
| Joulescope server used? | yes (auto range); some direct captures for fine traces |
| **Joulescope desktop app detached?** | yes (confirmed by operator) |
| **`qtmonitor` detached?** | yes (confirmed by operator) |
| Plan deviations | sub-10 s reduced to 9 s only; B7 (60 s) run before B4–B6; Phase C/D deferred; probe builds flashed for diagnosis |

A run from a dirty tree is not reproducible. Record it as dirty rather than
omitting the row; a report with that row blank cannot be trusted later.

### Phase A — resting states

Windows of 300 s, whole minutes, `charge/time` figure.

**Gate: quiescent states below 1 µA.** `CONFIGURED` and `HIBERNATING` each carry
a wake per minute, so they are recorded, not gated.

| ID | State | Gate | W1 (µA) | W2 (µA) | W3 (µA) | Mean | Pass |
| --- | --- | --- | --- | --- | --- | --- | --- |
| A1 | IDLE, clock set (300 s ×3) | **< 1 µA** | 0.2921 | 0.2926 | 0.2937 | **0.2928** | PASS |
| A2 | CONFIGURED (900 s ×2) | record | 0.5162 | 0.5168 | — | **0.5165** | recorded |
| A3 | FINISHED (300 s ×3) | **< 1 µA** | | | | | |
| A4 | HIBERNATING (900 s ×2) | record | | | — | | |
| A5 | IDLE again (300 s ×1, final shipping image after probe removal) | **< 1 µA** | 0.2860 | — | — | **0.2860** | PASS |

Expect the floor at a few hundred nA, not just "under 1 µA" — the sub-1 µA
average at 60 s leaves only a fraction of a µA for it. Confirm the Joulescope
was auto-ranging and that the two/three windows agree to a few percent.

**A1 vs A5:** **2.3 %** apart (0.2928 vs 0.2860). (Gate: within 20%. These are the same logical
state reached by two histories; a divergence is a finding even when both numbers
look fine.)

**A2 vs A1:** **0.2237 µA** — the cost of the CONFIGURED minute alarm (trace: wakes at exactly 60 s spacing).

**A4 vs A1:** ______ µA — the cost of the HIBERNATING minute alarm. Roughly one
wake per minute's worth of charge; if it were the hourly alarm the comment
claims, this difference would be ~60× smaller and A4 would sit near the IDLE
floor. See H3.

**Implied wake charge** (A2 − A1) × 60 s = **13.4 µC** per CONFIGURED wake (trace: one 0.5 ms block at 27.1 µA ⇒ 13.4 µC, consistent). A4 not measured.

### Phase B — sample period sweep

One full 60-sample block per window, measurement begun at least one block after
the tag reached RUNNING.

| ID | Period | Regime | Window | W1 (µA) | W2 (µA) | W3 (µA) | Mean |
| --- | --- | --- | --- | --- | --- | --- | --- |
| B1 | 1 s | Stop 2 | 60 s | | | | |
| B2 | 9 s | Stop 2 | 540 s | 530.69 | — | — | **530.7** (never sleeps) |
| B3 | 10 s | Shutdown | 600 s | 3.6948 | — | — | **3.6948** |
| B3′ | 10 s, stock delay path, trace | 35 s | 3.27 | — | — | 34.9 µC / 59 ms per event |
| B3″ | 10 s, LSI delay path (tried, reverted) | 35 s | 2.94 | — | — | 31.0 µC / 54 ms per event |
| B3‴ | 30 ms all-devices-off wait, sleep depth | trace | — | — | — | 140 µA flat — the floating PA2, since fixed |
| B4 | 10 s, LPTIM + PA2 analog | 60 s | 2.9742 | — | — | `Q_cycle` 26.82 µC |
| **B5** | **10 s, RTC Alarm A + PA2 analog** | 120/180 s | **1.7376** | — | — | **`Q_cycle` 14.49 µC**, waits 8–16 µA |
| A6 | IDLE, same build | 120 s | 0.2884 | — | — | unchanged by either fix |
| B4 | 15 s | Shutdown | 900 s | | | — | |
| B5 | 30 s | Shutdown | 1800 s | | — | — | |
| B6 | **90 s (default)** | Shutdown | 5400 s | | — | — | |
| B7 | 60 s (optional) | Shutdown | 3600 s | 0.8552 | — | — | **0.8552** |

**The 9 s / 10 s step (B2 → B3):** ______ µA. This is the cost of rebooting per
sample, measured rather than inferred — the two configurations differ only in
sleep mode.

### Phase B fit — Shutdown regime (B3–B6)

```sh
embedded/tools/prestag_power_model.py \
    --point 10:<B3> --point 15:<B4> --point 30:<B5> --point 90:<B6> \
    --capacity 5.5 --capacity 11 --target-days 365
```

`I_avg(T) = I_rest + Q_cycle / T`

| Quantity | Value | Expected | Meaning |
| --- | --- | --- | --- |
| `I_rest` | **0.2873 µA** | a few hundred nA | Shutdown floor: MCU + RTC + LPS27 + AT25 + board |
| `Q_cycle` | **34.08 µC** | **tens of µC** | charge per sample, incl. amortised header |
| `T_knee` | **118.6 s** | | sampling costs as much as resting |
| max residual | ______ µA | < 5% | |
| R² | ______ | | |

**`I_rest` vs measured A1:** ______ µA vs ______ µA. Two independent routes to
the same quantity; they must agree, and the fit is the one to distrust if they
do not.

A `Q_cycle` in the hundreds of µC, or any sweep point in the tens of µA, is a
broken measurement — most often a monitor still attached — not a slow tag.

Paste the tool's output verbatim below rather than only the summary — the
residuals are how a bad point is caught:

```
(paste prestag_power_model.py output)
```

**Stop 2 regime (B1–B2), for reference only** — two points, so the line is
exact and has no residual check. Not comparable to the Shutdown fit:

| Quantity | Value |
| --- | --- |
| `I_rest` (Stop 2) | ______ µA |
| `Q_cycle` (Stop 2) | ______ µC |

### Battery lifetime

From the fit. Nominal is the upper bound; derated is the deployment figure.

| Period | `I_avg` (µA) | 5.5 mAh (days) | 11 mAh (days) |
| --- | --- | --- | --- |
| 10 s | | | |
| 15 s | | | |
| 30 s | | | |
| 60 s | | | |
| **90 s (default)** | | | |
| 180 s | | | |
| ceiling (T→∞) | `I_rest` = | | |

### One-year budget

Target lifetime 365 days. Budget is `1000 × C / (365 × 24)`.

| Cell | Budget (µA) | `I_avg` at 90 s | Meets? | Margin |
| --- | --- | --- | --- | --- |
| 5.5 mAh | 0.628 | 0.666 (pred.) | **no** | −0.038 µA (21 days short) |
| 11 mAh | 1.256 | 0.666 (pred.) | **yes** | 1.9× |

`I_avg` at 60 s: **0.8552 µA** measured (expected **< 1 µA** for a healthy tag) — PASS.

5.5 mAh is expected to be marginal and decided by the floor and the derating,
not by the sample period. Record whether it clears; do not read a miss as a
build regression without comparing against the baseline.

Derating actually applied: ______ (`--derate`), justified by: ______________
(self-discharge figure for the cell, cutoff voltage, deployment temperature).

At ~1 µA the tag draws about 8.8 mAh a year, the same order as the cells, so
self-discharge is part of the answer rather than a correction to it. State the
cell's actual figure here: ______________

| Period | 5.5 mAh derated (days) | 11 mAh derated (days) |
| --- | --- | --- |
| 30 s | | |
| **90 s (default)** | | |

**Recommended deployment period:** ______ s, because ______________.
(Reference `T_knee`: below it, a longer period buys a lot; above it, little.)

### Phase C — schedule behaviour, `T = 10 s`

| ID | Test | Expected | Observed | Pass |
| --- | --- | --- | --- | --- |
| C1 | Scheduled start | CONFIGURED until start epoch; RUNNING ≤ 60 s after | held CONFIGURED with the start 120 s out; first sample **+17 s** after the epoch | **PASS** |
| C2 | Immediate start (`start_epoch = 0`) | RUNNING within ~60 s | `tag-start` returned `State: RUNNING` immediately | **PASS** |
| C3 | Scheduled stop | FINISHED within one period (10 s) of stop epoch | last sample **−3 s** before the epoch; 29 samples against an expected 30 | **PASS** |
| C4 | Commanded stop | `tag-stop` exits 0 **and** FINISHED confirmed | `tag-stop` reported `state: FINISHED` | **PASS** |
| C4b | Download straight after C4 | succeeds; no "Can't dump logs from current state" | downloaded cleanly, 1 record | **PASS** |
| C5 | Hibernation entry | at the next 60-sample block boundary at/after the window opens, not at the instant it opens | entered at **sample 120**, a block boundary, **+510 s** after the window opened | **PASS** (does not discriminate — see below) |
| C5b | Hibernation exit | RUNNING within ~60 s of window close | resumed **+66 s** after close | **PASS** |
| C5c | Run end | FINISHED at `end_epoch` | last sample t+2386 s against a 2400 s window | **PASS** |
| C6 | C3/C4 repeated at `T = 90 s` (default) | same behaviour | not run | — |
| T4 | Brownout recovery, odd page count | continuous data, no zero-sample block (regression test for the §1.7 fix) | not run | — |

**C5 does not discriminate the §1.7 cursor fix.** The window opened at t+690, so
the first 60-sample boundary at or after it is sample 120 — which is also a
multiple of 120, so the *old* gate would have entered at the same place. To test
the fix the window must open between samples 60 and 120 (e.g. t+300 to t+1500):
the corrected gate enters at t+600, the old one at t+1200. T4 remains the
stronger regression test, and neither has been run.

**Poll sparingly, and verify from the data.** The C4 run recorded only 3 samples
because state was polled eight times with `tag-info`, and every attach connects
under reset. C1/C3 recorded 29 of an expected 30 once polling was dropped in
favour of reading the epochs back from the download. The plan says this; it is
easy to ignore in the moment.

Latencies observed:

| Event | Latency | Note |
| --- | --- | --- |
| start command → CONFIGURED | immediate | reported by `tag-start` |
| start epoch → RUNNING | **+17 s** | minute-alarm polling, expect ≤ 60 s |
| stop epoch → FINISHED | **−3 s** (last sample) | within one 10 s period |
| `tag-stop` → FINISHED | immediate | reported by the stop itself; no polling needed |
| hibernate window open → HIBERNATING | **+510 s** | the next 60-sample boundary, as designed |
| hibernate window close → RUNNING | **+66 s** | minute-alarm granularity, expect ≤ ~60 s |

### H3 — hibernation wake cadence

```sh
embedded/tools/joulescope_measure.py --use-server --duration 300 --window 0.02
```

| Observation | Value |
| --- | --- |
| Wake events counted in 300 s | |
| Implied cadence | |
| Consistent with a **minute** alarm (expect 5)? | |
| Consistent with an **hour** alarm (expect 0–1)? | |

### Phase D — data verification

Run `prestag_check_download.py` on every downloaded database and paste its
verdict. Structure and values both, per §7:

```sh
embedded/tools/prestag_check_download.py <db> --period 10 \
    --expect-duration <s> --expect-gaps <n>
```

| Run | `--expect-gaps` | Samples found | Checker verdict | Notes |
| --- | --- | --- | --- | --- |
| 9 s diagnostic run | 0 | 50 | **PASS** | 990.56–990.94 hPa (7 distinct), 25.8–26.1 °C, 2.47 V, no sentinels |
| C1/C3 (one 300 s run) | 0 | 29 of ~30 | **PASS** | 989.13–989.31 hPa (4 distinct), 24.79–25.00 °C, 2.470 V |
| C4 | 0 | 3 | **PASS** | 989.31–989.38 hPa, 25.04–25.18 °C; short because state was polled |
| C5 | 1 | 164, 3 headers | **PASS** | one gap of 766 s at sample 120; 988.94–989.31 hPa, 24.67–25.55 °C |
| T4 | 0 | not run | — | |

Value checks, worst case seen across all runs:

| Check | Bound | Worst observed | Pass |
| --- | --- | --- | --- |
| Pressure range | > 900 hPa (and < 1100) | 988.94 … 990.94 hPa across four runs | **PASS** |
| Temperature range | 15–40 °C | 24.67 … 26.1 °C | **PASS** |
| Failed-read sentinel (−2048.00 hPa / −327.68 °C) | **none** | none in any run | **PASS** |
| Distinct pressure values | > 1 | | |
| Voltage range | 2.0–3.7 V | | |

Mean pressure ______ hPa against local **station** pressure ______ hPa at
______ (not the sea-level-adjusted figure); difference ______ hPa.

Mean temperature ______ °C against room ______ °C.

C5 hibernation gap:

| Item | Value |
| --- | --- |
| Number of gaps found | (expect exactly 1) |
| Gap start epoch | |
| Gap end epoch | |
| Matches observed entry/exit? | |
| Clean absence, not wrong timestamps? | |

Remember `power_experiment.check_download()` derives its expected rate from
`lsm6.odr` and returns `None` for a PresTag, so **its rate check silently does
not run**. The sample-count column above must be filled in by hand or by a
PresTag-aware check.

### T4 — brownout recovery detail

| Item | Value |
| --- | --- |
| Header count before brownout (must be **odd**) | |
| Brownout genuinely induced, not a probe reset? | |
| Index of the zero-sample block, if any | |
| Data continuous across recovery? | |
| Displacement observed (blocks) | |

A probe reset is classified as a monitor attach and resumes via `T_CONT`, which
never reaches the round-up. If a true brownout could not be induced, record that
the test did not run rather than recording a pass.

### Deviations confirmed

The plan's §10 predicts three places where code and comments disagree. Record
what was actually observed; each confirmed item should be filed against the
firmware.

**Regressions on the §1.7 fix** — these must come back *clean*; a "confirmed"
here is a regression, not a discovery:

| # | Must NOT happen | Clean? | Evidence |
| --- | --- | --- | --- |
| R1 | Hibernation entry skipped at a 60-sample block boundary | entry at sample 120, a boundary — but see the C5 caveat: this run does not discriminate the fix | C5 |
| R2 | Zero-sample block or one-block displacement after brownout recovery | not tested | T4 |

**Still open** — a "confirmed" here is expected, and should be filed:

| # | Prediction | Confirmed? | Evidence | Filed as |
| --- | --- | --- | --- | --- |
| 1 | `ALARM_HOUR` behaves as `ALARM_MINUTE`; hibernation wakes 60×/hour | consistent: CONFIGURED wakes every 60.0 s (traced) and hibernation exited +66 s after the window closed | A2, H3, C5b | |
| 2 | `start_delay` ignored, so `--start-now` is a no-op on PresTag | yes — start is governed solely by `active_interval.start_epoch` (C1 held CONFIGURED, C2 started at once) | C1, C2 | |

### Findings and follow-ups

| # | Finding | Severity | Action |
| --- | --- | --- | --- |
| F1 | **RESOLVED.** `stopMilliseconds()` did not reach Stop 2 because PA2/INT1 was a floating digital input dissipating ~130 µA, and the LPTIM re-arm cost 6.3–7.1 ms of Run current per delay. Pin made analog (`bf0c331`) and the delay moved to a free-running RTC Alarm A tick (`0ac8bc6`). Stop-delay plateau 143 µA → 8–16 µA; `Q_cycle` 34.06 → 14.49 µC. | was high | done |
| F2 | `godown(STOP2)` is a silent no-op on L432: `tagPowerEnterTerminalSleep()` handles only Standby and Shutdown and returns for anything else. Sub-10 s periods never sleep (530.7 µA flat). | medium — bench only | implement or reject STOP2 explicitly rather than returning silently |
| F3 | `writeStoredConfig()` ignores `FLASH_Program_Array()`'s result and `erasePersistent()` never checks its erase, so a stale `sconfig` survives a reset-and-start. `tag-start` printed `period: 10` while the tag ran at 9 s. | high — silent wrong configuration | check flash status on the config write path |
| F4 | A free-running timer used for delays freezes ChibiOS time when it genuinely reaches Stop 2, because the OS tick is TIM2. With one thread this is mostly harmless, but any pending virtual timer (e.g. the 10 s monitor attach grace) then never expires. | medium — blocks any Stop 2 delay work | skip Stop 2 while `chVTGetTimersStateI()` reports a pending timer |
| F5 | `PresTagRaw` never received the LPS27 timing reduction from `f2a82b5`: it still uses the driver defaults (10 ms power-up, up to 6×15 ms polling) where `PresTag` uses 5/5/1. | low | apply the same constants |

### Overall result

| Gate | Result |
| --- | --- |
| IDLE and FINISHED (A1, A3, A5) below 1 µA | |
| A1/A5 within 20% | |
| Fit `I_rest` below 1 µA and consistent with A1 | |
| `I_avg` at 60 s below 1 µA | |
| `I_avg` at 90 s meets the 11 mAh one-year budget (1.256 µA) | |
| `I_avg` at 90 s meets the 5.5 mAh one-year budget (0.628 µA) — expected marginal | |
| All Phase C expectations met | **yes** — C1, C2, C3, C4, C4b, C5, C5b, C5c all pass; C6 and T4 not run |
| All Phase D checks passed | **yes** — four downloads, structure and values, no sentinels |
| Shutdown fit linear (residuals < 5%) | two-point fit; B4/B5/B6 not run, so no residual available |
| **Session verdict** | **Power and schedule behaviour pass; Stop 2 now works and both cells clear a year at the 90 s default (5.5 mAh 510 d, 11 mAh 1020 d, projected from `Q_cycle` = 14.49 µC).** Not a release qualification: the 30/60/90 s sweep points are extrapolated rather than measured, C6, T4 and H3's event count are outstanding, PresTagRaw is opted in but untested, and F3 (unchecked flash status on the config write) is still open. |

---

## Baseline

Once one session passes cleanly, promote its numbers here. Later sessions
compare against this table; a move of more than 20% in any row is a finding to
investigate before shipping, not a number to record and move past.

| Quantity | Baseline | Set by session | Date |
| --- | --- | --- | --- |
| IDLE (clock set) | | | |
| CONFIGURED | | | |
| FINISHED | | | |
| HIBERNATING | | | |
| `I_rest` (Shutdown fit) | | | |
| `Q_cycle` | | | |
| `T_knee` | | | |
| `I_avg` at 60 s | | | |
| `I_avg` at 90 s (default) | | | |

## Session log

| Date | git hash | Dirty | Verdict | Notes |
| --- | --- | --- | --- | --- |
| | | | | |
