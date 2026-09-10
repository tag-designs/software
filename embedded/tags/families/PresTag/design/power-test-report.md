# PresTag Power and Schedule Test Report

Results for the procedure in [`power-test-plan.md`](power-test-plan.md). One
section per session; keep old sessions rather than overwriting them, so a
regression can be bisected against a number someone actually took.

**Status: campaign executed 2026-09-08 to 2026-09-09.** Phases A, B, C and D
are complete on the shipping build (`890a11b`), with the power model measured at
three periods rather than extrapolated. **T4 (brownout recovery) is the one test
not run**, and F3 (unchecked flash status on the config write) is still open.
Summary and interpretation are in the plan's §11.

---

## Session template

Copy this whole block for each run.

### Provenance

| Item | Value |
| --- | --- |
| Date (UTC) | 2026-09-08 (diagnosis) through 2026-09-09 (final measurements) |
| Operator | G. Brown / Claude (Claude Code) |
| git hash | final numbers at `890a11b`; diagnosis began at `411b046` |
| **Tree dirty?** | during diagnosis yes (probe builds, tooling fixes); the final sweep, C6, H3 and both PresTagRaw runs were taken on committed firmware |
| Target built | `PresTag`, plus `PresTagRaw` for the variant comparison |
| `tag-info` firmware string | PresTagv4, Firmware version 1 |
| Board / serial | PresTagv3 board on the baseboard rig |
| Supply | baseboard via Joulescope JS320, **2.485 V** |
| Joulescope interpreter | /home/geobrown/opt/joulescope-mcp/.venv/bin/python |
| Joulescope server used? | yes (auto range), one server held open for the session |
| **Joulescope desktop app detached?** | yes (confirmed by operator) |
| **`qtmonitor` detached?** | yes (confirmed by operator) |
| Plan deviations | sub-10 s reduced to 9 s only (F2 makes the regime uninformative); B4/B5 (15 s, 30 s) not measured — 60 s and 90 s were measured instead, which is where the deployment sits; H3 traced at the server's 0.5 s block rather than 0.02 s; T4 not run |

Numbers taken during diagnosis are labelled with the build they came from, and
are kept because they measure the faults that were fixed. The gate table at the
end is judged only on the `890a11b` figures.

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
| A3 | FINISHED (300 s ×3) | **< 1 µA** | 0.2796 | 0.2793 | 0.2782 | **0.2790** | PASS |
| A4 | HIBERNATING (295 s ×2, inside the H3 window) | record | 0.3769 | 0.3769 | — | **0.3769** | recorded |
| A5 | IDLE again (300 s ×1, final shipping image after probe removal) | **< 1 µA** | 0.2860 | — | — | **0.2860** | PASS |

Expect the floor at a few hundred nA, not just "under 1 µA" — the sub-1 µA
average at 60 s leaves only a fraction of a µA for it. Confirm the Joulescope
was auto-ranging and that the two/three windows agree to a few percent.

**A1 vs A5:** **2.3 %** apart (0.2928 vs 0.2860). (Gate: within 20%. These are the same logical
state reached by two histories; a divergence is a finding even when both numbers
look fine.)

**The four resting measurements agree:** IDLE 0.2928 (A1), IDLE again 0.2860
(A5), FINISHED 0.2790 (A3), and the IDLE window taken with the final sweep
0.2810 (A1′) — a **4.7 %** total spread across four states-and-histories and two
days, against a 20 % gate. FINISHED rests as deeply as IDLE, which is the point
of the check: a tag left at the end of a deployment costs no more than one that
was never started.

**A2 vs A1:** **0.2237 µA** — the cost of the CONFIGURED minute alarm (trace: wakes at exactly 60 s spacing).

**A4 vs A1:** **0.0841 µA** — the cost of the HIBERNATING minute alarm, i.e.
**5.05 µC** per wake. Had it been the hourly alarm the comment claims, this
difference would be ~60× smaller and A4 would sit on the IDLE floor. H3 counted
the wakes directly and confirms the minute alarm.

**Implied wake charge** (A2 − A1) × 60 s = **13.4 µC** per CONFIGURED wake (trace: one 0.5 ms block at 27.1 µA ⇒ 13.4 µC, consistent). For HIBERNATING, (A4 − A1) × 60 s = **5.05 µC** per wake, cross-checked by its peak block at (11.80 − 0.28) × 0.5 = 5.76 µC — cheaper than a CONFIGURED wake because it checks the clock without sampling.

### Phase B — sample period sweep

One full 60-sample block per window, measurement begun at least one block after
the tag reached RUNNING.

**Final sweep — the shipping build** (`890a11b`: PA2 analog, RTC Alarm A
ticker). Every point is a measurement over a whole number of 60-sample blocks,
not an extrapolation:

| ID | Period | Regime | Window | Measured |
| --- | --- | --- | --- | --- |
| A1′ | IDLE | Shutdown | 300 s | **0.2810 µA** |
| B3 | 10 s | Shutdown | 600 s | **1.8099 µA** |
| B7 | 60 s | Shutdown | 3600 s | **0.5406 µA** |
| **B6** | **90 s (default)** | Shutdown | 5400 s | **0.4517 µA** |
| B2 | 9 s | Stop 2 | 540 s | **530.7 µA** — never sleeps, see F2 |

**The 9 s / 10 s step (B2 → B3):** **528.9 µA**. This is the cost of *not*
rebooting per sample: below 10 s the tag stays up and `godown(STOP2)` returns
without sleeping (F2), so the 9 s point measures a tag that never rests. It is
not a point on the Shutdown line and is excluded from the fit.

**Progression at `T = 10 s`**, showing what each fix was worth — same tag, same
period, four builds:

| Build | Measured | `Q_cycle` |
| --- | --- | --- |
| stock: LPTIM delay, PA2 a floating input | 3.6948 µA | 34.06 µC |
| PA2 made analog (`bf0c331`) | 2.9742 µA | 26.82 µC |
| plus the RTC Alarm A ticker (`0ac8bc6`), first window | 1.7376 µA | 14.49 µC |
| **shipping, full 600 s block** | **1.8099 µA** | **15.26 µC** |

Two diagnostic traces from the same period, kept because they measure the fault
rather than the fix: the stock delay path spent 59 ms per event at 34.9 µC, and
an all-devices-off wait sat at **140 µA flat** — the floating PA2, before it was
found. IDLE was **0.2884 µA** across all of these, unmoved by either fix, which
is what pinned the cost to the sampling cycle rather than to the floor.

### Phase B fit — Shutdown regime (B3, B7, B6)

```sh
embedded/tools/prestag_power_model.py \
    --point 10:1.8099 --point 60:0.5406 --point 90:0.4517 \
    --capacity 5.5 --capacity 11 --target-days 365
```

`I_avg(T) = I_rest + Q_cycle / T`

| Quantity | Value | Expected | Meaning |
| --- | --- | --- | --- |
| `I_rest` | **0.2842 µA** | a few hundred nA | Shutdown floor: MCU + RTC + LPS27 + AT25 + board |
| `Q_cycle` | **15.26 µC** | **tens of µC** | charge per sample, incl. amortised header |
| `T_knee` | **53.7 s** | | sampling costs as much as resting |
| max residual | **0.0021 µA** (0.4%) | < 5% | at `T` = 60 s |
| R² | **0.999993** | | three points, one degree of freedom |

**`I_rest` vs measured A1:** **0.2842 µA** fitted vs **0.2810 µA** measured
(A1′, the same-session IDLE window) — **1.1%** apart. Two independent routes to
the same quantity; they agree, so neither is in doubt. Against the earlier
three-window A1 of 0.2928 µA the gap is 2.9%, still well inside the gate.

A third, independent check on `Q_cycle`: the in-run trace during C6a saw two
wake events 90.0 s apart on a 0.280 µA floor at a 0.4332 µA mean, giving
(0.4332 − 0.280) × 199.5 ÷ 2 = **15.3 µC** — the same number from block-level
event charge rather than from the slope of a three-point fit.

A `Q_cycle` in the hundreds of µC, or any sweep point in the tens of µA, is a
broken measurement — most often a monitor still attached — not a slow tag.

The tool's output verbatim — the residuals are how a bad point is caught:

```
PresTag Shutdown-regime average-current model
  I_avg(T) = I_rest + Q_cycle / T

Measurements
  T =    10.00 s   measured      1.810 uA   model      1.810 uA   residual   -0.000 uA
  T =    60.00 s   measured      0.541 uA   model      0.538 uA   residual   +0.002 uA
  T =    90.00 s   measured      0.452 uA   model      0.454 uA   residual   -0.002 uA

Fit
  I_rest              0.2842 uA      resting Shutdown current
  Q_cycle            15.2588 uC      charge per sampling cycle
  max residual        0.0021 uA
  R^2               0.999993
  T_knee               53.70 s       sampling costs as much as resting
```

**Stop 2 regime (B2), for reference only** — not comparable to the Shutdown fit:

| Quantity | Value |
| --- | --- |
| `I_rest` (Stop 2) | not fitted — only one usable point (B2, 9 s) |
| `Q_cycle` (Stop 2) | not fitted |

There is no Stop 2 line to fit. F2 is the reason: below 10 s `godown(STOP2)` is
a silent no-op on this part, so the tag never rests between samples and B2
measures a continuously running tag (530.7 µA) rather than a duty cycle. Two
points would be needed and the second would measure the same thing.

### Battery lifetime

From the fit. Nominal is the upper bound; derated is the deployment figure.

| Period | `I_avg` (µA) | 5.5 mAh (days) | 11 mAh (days) |
| --- | --- | --- | --- |
| 10 s | 1.810 | 127 | 253 |
| 15 s | 1.301 | 176 | 352 |
| 30 s | 0.793 | 289 | 578 |
| 60 s | 0.539 | 426 | 851 |
| **90 s (default)** | **0.454** | **505** | **1010** |
| 180 s | 0.369 | 621 | 1242 |
| ceiling (T→∞) | `I_rest` = 0.284 | 806 | 1613 |

Bold rows are measured; 15 s, 30 s and 180 s are the model evaluated between
measured points, on a fit whose worst residual is 0.4%.

### One-year budget

Target lifetime 365 days. Budget is `1000 × C / (365 × 24)`.

| Cell | Budget (µA) | `I_avg` at 90 s | Meets? | Margin |
| --- | --- | --- | --- | --- |
| 5.5 mAh | 0.628 | **0.4517 measured** | **yes** | +0.176 µA (1.4×, 505 days) |
| 11 mAh | 1.256 | **0.4517 measured** | **yes** | 2.8× (1010 days) |

Both cells clear a year at the shipped period. 5.5 mAh was **21 days short**
before the PA2 and Alarm A fixes, on a predicted 0.666 µA; the measured 0.4517 µA
is what moved it.

`I_avg` at 60 s: **0.5406 µA** measured (expected **< 1 µA** for a healthy tag) — PASS.

5.5 mAh is expected to be marginal and decided by the floor and the derating,
not by the sample period. Record whether it clears; do not read a miss as a
build regression without comparing against the baseline.

Derating actually applied: **0.75** (`--derate 0.75`), as a placeholder pending
a cell datasheet. It is **not** justified from a real self-discharge figure,
cutoff voltage or deployment temperature — those numbers have not been supplied,
so treat the derated column as an illustration of sensitivity, not as a
deployment guarantee.

At ~0.45 µA the tag draws about 4.0 mAh a year, comparable to the cells
themselves, so self-discharge is part of the answer rather than a correction to
it. The cell's actual figure still needs to be stated here: ______________

| Period | 5.5 mAh derated (days) | 11 mAh derated (days) |
| --- | --- | --- |
| 30 s | 217 | 434 |
| **90 s (default)** | **379** | **758** |

Even at 75% both cells clear a year at 90 s.

**Recommended deployment period: 90 s**, the shipped default — no change needed.
`T_knee` is **53.7 s**, so 90 s already sits above the knee: going to 180 s buys
5.5 mAh only another 116 days (505 → 621) while halving the data rate, and
dropping to 60 s costs 79 days. The interesting question is not the period but
the floor: at `T` ≫ `T_knee` the lifetime asymptote is 806 days on 5.5 mAh,
set entirely by `I_rest`.

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
| C6a | Scheduled stop at `T = 90 s` | FINISHED within one period (90 s) of the stop epoch | FINISHED **+1 s** (`EVENT_ENDTIM`); 5 samples at exactly 90 s, the sample due at the stop epoch correctly not taken; download PASS | **PASS** |
| C6b | Commanded stop at `T = 90 s` | `tag-stop` exits 0 **and** FINISHED confirmed; immediate download works | exit 0 reporting `state: FINISHED`, confirmed in **1 poll at +1 s**; immediate download clean, 3 samples at 90 s, PASS | **PASS** |
| T4 | Brownout recovery, odd page count | continuous data, no zero-sample block (regression test for the §1.7 fix) | not run | — |

**C5 does not discriminate the §1.7 cursor fix — H3's run does.** C5's window
opened at t+690, so the first 60-sample boundary at or after it is sample 120,
which is also a multiple of 120: the *old* gate would have entered at the same
place. H3 was therefore configured with the window at **t+300 … t+1500**, which
opens between samples 60 and 120, and entry was logged one period after
**sample 60**. The old gate would have waited for sample 120. The fix is
confirmed on hardware. T4 remains the stronger regression test — it exercises
brownout recovery rather than the hibernation gate — and has not been run.

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
| hibernate window open → HIBERNATING | **+510 s** (C5), **+310 s** (H3) | the next 60-sample boundary in both, as designed; the offset is set by where the window opens within the block |
| hibernate window close → RUNNING | **+66 s** (C5), **+17 s** (H3) | minute-alarm granularity, expect ≤ ~60 s |
| stop epoch → FINISHED at `T = 90 s` | **+1 s** | C6a |
| stop epoch → FINISHED at `T = 10 s` | **+7 s** | H3's run end |
| `tag-stop` → FINISHED at `T = 90 s` | immediate, confirmed at +1 s by one poll | C6b |

### H3 — hibernation wake cadence

```sh
embedded/tools/joulescope_measure.py --use-server --duration 300 --window 0.02
```

Run at `period: 10` for 1800 s with the hibernate window at t+300 … t+1500, and
three 295 s traces taken at the server's 0.5 s block (the plan's 0.02 s window
is not used: changing the window on a running server races inside the driver and
has wedged the instrument, and a wake occupies one whole block at either size).

| Trace | When | Mean | Events | Spacing | Peak |
| --- | --- | --- | --- | --- | --- |
| A — RUNNING at 10 s (reference) | run+60…360 | 1.8100 µA | 29 | **10.0 s** | ~32.4 µA |
| B — HIBERNATING | run+660…960 | 0.3769 µA | **5** | **60.0 s** | ~11.8 µA |
| C — HIBERNATING | run+1140…1440 | 0.3769 µA | **5** | **60.0 s** | ~11.8 µA |

| Observation | Value |
| --- | --- |
| Wake events counted in 295 s | **5**, twice |
| Implied cadence | **once per 60.0 s** |
| Consistent with a **minute** alarm (expect 5)? | **yes** |
| Consistent with an **hour** alarm (expect 0–1)? | **no** |

Trace A validates the method: its 1.8100 µA matches the independently measured
10 s sweep point of 1.8099 µA to four digits, so the block trace and the
charge/time average are measuring the same thing.

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
| C5 re-run (Alarm A build) | 1 | 165, 3 headers | **PASS** | one gap of 759 s; 988.75–988.94 hPa, 25.09–25.25 °C |
| PresTagRaw | 0 | 73, 2 blocks | **PASS** | 985.13–985.56 hPa, 24.31–24.98 °C |
| C6a (90 s, scheduled stop) | 0 | 5, 1 header | **PASS** | 985.06–985.19 hPa, 24.62–24.95 °C, spacing exactly 90 s |
| C6b (90 s, commanded stop) | 0 | 3, 1 header | **PASS** | 985.00–985.13 hPa, 24.82–24.96 °C; downloaded immediately after the stop |
| H3 (10 s, hibernation) | 1 | 88, 2 headers | **PASS** | one gap of 927 s **at sample 60**; 985.00–985.75 hPa, 24.29–25.00 °C |
| T4 | 0 | not run | — | needs a genuine brownout; see below |

Value checks, worst case seen across all runs:

| Check | Bound | Worst observed | Pass |
| --- | --- | --- | --- |
| Pressure range | > 900 hPa (and < 1100) | 988.94 … 990.94 hPa across four runs | **PASS** |
| Temperature range | 15–40 °C | 24.67 … 26.1 °C | **PASS** |
| Failed-read sentinel (−2048.00 hPa / −327.68 °C) | **none** | none in any run | **PASS** |
| Distinct pressure values | > 1 | 3 (C6b, 3 samples) — never 1, so never a stuck sensor | **PASS** |
| Voltage range | 2.0–3.7 V | 2.470 V in every run | **PASS** |

Pressure sits at **985.0 … 990.9 hPa** across nine downloads spanning two days,
drifting a few hPa between runs hours apart — real weather, not a stuck reading.
It has not been compared against a local **station** pressure reading (the
sea-level-adjusted figure that weather sites publish is not the right
comparison), so the absolute calibration is unverified; only plausibility and
self-consistency are established here. Local station pressure ______ hPa at
______; difference ______ hPa.

Mean temperature **24.3 … 26.1 °C**, consistent with a bench indoors, against a
room temperature that was not independently recorded: ______ °C. The tag's own
die temperature read 27.2–28.4 °C in the same runs, a plausible couple of
degrees above ambient.

Hibernation gap, H3's run (the discriminating one):

| Item | Value |
| --- | --- |
| Number of gaps found | **1** (expect exactly 1) |
| Gap start epoch | 1789001623 (00:53:43 UTC) — **sample 60** |
| Gap end epoch | 1789002550 (01:09:10 UTC) |
| Matches observed entry/exit? | **yes** — `HIBERNATING` logged 00:53:53, `RUNNING` 01:09:00; the gap is bounded by the last sample before and the first after |
| Clean absence, not wrong timestamps? | **yes** — 88 samples, epochs monotonic, 10 s spacing either side; the 927 s gap is missing data, not displaced data |

C5's earlier runs each showed exactly one gap too (766 s and 759 s, both at
sample 120).

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
| R1 | Hibernation entry skipped at a 60-sample block boundary | **clean, and now discriminating.** H3's run opened the window at t+300, between samples 60 and 120. Entry was logged at 00:53:53, one period after **sample 60**; the old gate would have waited for sample 120 at 01:03:43, and traces B and C would have shown the 10 s cadence rather than the minute one. The download's single gap sits exactly at sample 60. | H3, C5 |
| R2 | Zero-sample block or one-block displacement after brownout recovery | not tested | T4 |

**Still open** — a "confirmed" here is expected, and should be filed:

| # | Prediction | Confirmed? | Evidence | Filed as |
| --- | --- | --- | --- | --- |
| 1 | `ALARM_HOUR` behaves as `ALARM_MINUTE`; hibernation wakes 60×/hour | **yes, measured.** Five wake events in 295 s at exactly 60.0 s spacing, twice over, at 5.05 µC each | A2, A4, H3, C5b | |
| 2 | `start_delay` ignored, so `--start-now` is a no-op on PresTag | yes — start is governed solely by `active_interval.start_epoch` (C1 held CONFIGURED, C2 started at once) | C1, C2 | |

### Findings and follow-ups

| # | Finding | Severity | Action |
| --- | --- | --- | --- |
| F1 | **RESOLVED.** `stopMilliseconds()` did not reach Stop 2 because PA2/INT1 was a floating digital input dissipating ~130 µA, and the LPTIM re-arm cost 6.3–7.1 ms of Run current per delay. Pin made analog (`bf0c331`) and the delay moved to a free-running RTC Alarm A tick (`0ac8bc6`). Stop-delay plateau 143 µA → 8–16 µA; `Q_cycle` 34.06 → 14.49 µC. | was high | done |
| F2 | `godown(STOP2)` is a silent no-op on L432: `tagPowerEnterTerminalSleep()` handles only Standby and Shutdown and returns for anything else. Sub-10 s periods never sleep (530.7 µA flat). | medium — bench only | implement or reject STOP2 explicitly rather than returning silently |
| F3 | `writeStoredConfig()` ignores `FLASH_Program_Array()`'s result and `erasePersistent()` never checks its erase, so a stale `sconfig` survives a reset-and-start. `tag-start` printed `period: 10` while the tag ran at 9 s. | high — silent wrong configuration | check flash status on the config write path |
| F4 | **ADDRESSED.** A free-running timer used for delays freezes ChibiOS time when it genuinely reaches Stop 2, because the OS tick is TIM2. With one thread this is mostly harmless, but any pending virtual timer (e.g. the 10 s monitor attach grace) then never expires. | medium | done — `stopMilliseconds()` falls back to `chThdSleepMilliseconds()` when `chVTGetTimersStateI()` reports a pending timer, or when the monitor is attached (`0ac8bc6`) |
| F5 | **RESOLVED.** `PresTagRaw` never received the LPS27 timing reduction from `f2a82b5`, and had also silently inherited `STANDBY` for all five sleep states where `PresTag` uses `SHUTDOWN`. | low | done (`4527184`) — `custom.h` aligned, then measured: IDLE 0.2792 µA and 10 s run 1.7746 µA, both within 2% of PresTag, download PASS |

### Overall result

| Gate | Result |
| --- | --- |
| IDLE and FINISHED (A1, A3, A5) below 1 µA | **PASS** — 0.2928 / 0.2790 / 0.2860 µA, all ~3.5× under |
| A1/A5 within 20% | **PASS** — 2.3% |
| Fit `I_rest` below 1 µA and consistent with A1 | **PASS** — 0.2842 µA fitted, 1.1% from the measured 0.2810 µA |
| `I_avg` at 60 s below 1 µA | **PASS** — **0.5406 µA** measured |
| `I_avg` at 90 s meets the 11 mAh one-year budget (1.256 µA) | **PASS** — 0.4517 µA, 2.8× margin, 1010 days |
| `I_avg` at 90 s meets the 5.5 mAh one-year budget (0.628 µA) — expected marginal | **PASS** — 0.4517 µA, 1.4× margin, 505 days |
| All Phase C expectations met | **yes** — C1, C2, C3, C4, C4b, C5, C5b, C5c, **C6a, C6b** all pass; T4 not run |
| All Phase D checks passed | **yes** — nine downloads, structure and values, no sentinels |
| Shutdown fit linear (residuals < 5%) | **PASS** — three measured points, max residual **0.4%**, R² 0.999993 |
| **Session verdict** | **Pass.** Stop 2 works, the power model is measured at three periods rather than extrapolated (`I_rest` 0.2842 µA, `Q_cycle` 15.26 µC, `T_knee` 53.7 s, R² 0.999993), and **both cells clear a year at the shipped 90 s period** — 5.5 mAh 505 days, 11 mAh 1010 days, and 379 / 758 days at a 75% derating. Schedule behaviour is correct at both 10 s and 90 s, hibernation wakes once a minute as measured, and the §1.7 cursor fix is confirmed on hardware by a run configured to discriminate it. **Not yet a release qualification:** T4 (brownout recovery) has not been run, and F3 — `writeStoredConfig()` ignoring the flash status, which can leave a stale configuration running silently — is still open. |

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
