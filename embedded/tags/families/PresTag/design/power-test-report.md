# PresTag Power and Schedule Test Report

Results for the procedure in [`power-test-plan.md`](power-test-plan.md). One
section per session; keep old sessions rather than overwriting them, so a
regression can be bisected against a number someone actually took.

**Status: not yet executed.** The tables below are the blank form. Nothing here
is a measurement until a session fills it in and signs the provenance block.

---

## Session template

Copy this whole block for each run.

### Provenance

| Item | Value |
| --- | --- |
| Date (UTC) | |
| Operator | |
| git hash | |
| **Tree dirty?** | |
| Target built | `PresTag` |
| `tag-info` firmware string | |
| Board / serial | |
| Supply | baseboard via Joulescope, __ V |
| Joulescope interpreter | |
| Joulescope server used? | |
| **Joulescope desktop app detached?** | |
| **`qtmonitor` detached?** | |
| Plan deviations | |

A run from a dirty tree is not reproducible. Record it as dirty rather than
omitting the row; a report with that row blank cannot be trusted later.

### Phase A — resting states

Windows of 300 s, whole minutes, `charge/time` figure.

**Gate: quiescent states below 1 µA.** `CONFIGURED` and `HIBERNATING` each carry
a wake per minute, so they are recorded, not gated.

| ID | State | Gate | W1 (µA) | W2 (µA) | W3 (µA) | Mean | Pass |
| --- | --- | --- | --- | --- | --- | --- | --- |
| A1 | IDLE, clock set (300 s ×3) | **< 1 µA** | | | | | |
| A2 | CONFIGURED (900 s ×2) | record | | | — | | |
| A3 | FINISHED (300 s ×3) | **< 1 µA** | | | | | |
| A4 | HIBERNATING (900 s ×2) | record | | | — | | |
| A5 | IDLE again (300 s ×3) | **< 1 µA** | | | | | |

Expect the floor at a few hundred nA, not just "under 1 µA" — the sub-1 µA
average at 60 s leaves only a fraction of a µA for it. Confirm the Joulescope
was auto-ranging and that the two/three windows agree to a few percent.

**A1 vs A5:** ______ % apart. (Gate: within 20%. These are the same logical
state reached by two histories; a divergence is a finding even when both numbers
look fine.)

**A2 vs A1:** ______ µA — the cost of the CONFIGURED minute alarm.

**A4 vs A1:** ______ µA — the cost of the HIBERNATING minute alarm. Roughly one
wake per minute's worth of charge; if it were the hourly alarm the comment
claims, this difference would be ~60× smaller and A4 would sit near the IDLE
floor. See H3.

**Implied wake charge** (A4 − A1) × 60 s = ______ µC per wake.

### Phase B — sample period sweep

One full 60-sample block per window, measurement begun at least one block after
the tag reached RUNNING.

| ID | Period | Regime | Window | W1 (µA) | W2 (µA) | W3 (µA) | Mean |
| --- | --- | --- | --- | --- | --- | --- | --- |
| B1 | 1 s | Stop 2 | 60 s | | | | |
| B2 | 9 s | Stop 2 | 540 s | | | — | |
| B3 | 10 s | Shutdown | 600 s | | | — | |
| B4 | 15 s | Shutdown | 900 s | | | — | |
| B5 | 30 s | Shutdown | 1800 s | | — | — | |
| B6 | **90 s (default)** | Shutdown | 5400 s | | — | — | |
| B7 | 60 s (optional) | Shutdown | 3600 s | | — | — | |

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
| `I_rest` | ______ µA | a few hundred nA | Shutdown floor: MCU + RTC + LPS27 + AT25 + board |
| `Q_cycle` | ______ µC | **tens of µC** | charge per sample, incl. amortised header |
| `T_knee` | ______ s | | sampling costs as much as resting |
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
| 5.5 mAh | 0.628 | | | |
| 11 mAh | 1.256 | | | |

`I_avg` at 60 s: ______ µA (expected **< 1 µA** for a healthy tag).

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
| C1 | Scheduled start | CONFIGURED until start epoch; RUNNING ≤ 60 s after | | |
| C2 | Immediate start (`start_epoch = 0`) | RUNNING within ~60 s | | |
| C3 | Scheduled stop | FINISHED within one period (10 s) of stop epoch | | |
| C4 | Commanded stop | `tag-stop` exits 0 **and** FINISHED confirmed by polling | | |
| C4b | Download straight after C4 | succeeds; no "Can't dump logs from current state" | | |
| C5 | Hibernation entry | at the next 60-sample block boundary at/after the window opens, not at the instant it opens | | |
| C5b | Hibernation exit | RUNNING within ~60 s of window close | | |
| C5c | Run end | FINISHED at `end_epoch` | | |
| C6 | C3/C4 repeated at `T = 90 s` (default) | same behaviour | | |
| T4 | Brownout recovery, odd page count | continuous data, no zero-sample block (regression test for the §1.7 fix) | | |

Latencies observed:

| Event | Latency | Note |
| --- | --- | --- |
| start command → CONFIGURED | | |
| start epoch → RUNNING | | minute-alarm polling, expect ≤ 60 s |
| stop epoch → FINISHED | | expect ≤ one sample period |
| `tag-stop` → FINISHED | | number of polls needed: ____ |
| hibernate window open → HIBERNATING | | expect the next 60-sample boundary |
| hibernate window close → RUNNING | | expect ≤ 60 s |

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
| C1 | 0 | | | |
| C3 | 0 | | | |
| C4 | 0 | | | |
| C5 | 1 | | | |
| T4 | 0 | | | |

Value checks, worst case seen across all runs:

| Check | Bound | Worst observed | Pass |
| --- | --- | --- | --- |
| Pressure range | > 900 hPa (and < 1100) | | |
| Temperature range | 15–40 °C | | |
| Failed-read sentinel (−2048.00 hPa / −327.68 °C) | **none** | | |
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
| R1 | Hibernation entry skipped at a 60-sample block boundary | | C5 |
| R2 | Zero-sample block or one-block displacement after brownout recovery | | T4 |

**Still open** — a "confirmed" here is expected, and should be filed:

| # | Prediction | Confirmed? | Evidence | Filed as |
| --- | --- | --- | --- | --- |
| 1 | `ALARM_HOUR` behaves as `ALARM_MINUTE`; hibernation wakes 60×/hour | | H3 | |
| 2 | `start_delay` ignored, so `--start-now` is a no-op on PresTag | | C2 | |

### Findings and follow-ups

| # | Finding | Severity | Action |
| --- | --- | --- | --- |
| | | | |

### Overall result

| Gate | Result |
| --- | --- |
| IDLE and FINISHED (A1, A3, A5) below 1 µA | |
| A1/A5 within 20% | |
| Fit `I_rest` below 1 µA and consistent with A1 | |
| `I_avg` at 60 s below 1 µA | |
| `I_avg` at 90 s meets the 11 mAh one-year budget (1.256 µA) | |
| `I_avg` at 90 s meets the 5.5 mAh one-year budget (0.628 µA) — expected marginal | |
| All Phase C expectations met | |
| All Phase D checks passed | |
| Shutdown fit linear (residuals < 5%) | |
| **Session verdict** | |

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
