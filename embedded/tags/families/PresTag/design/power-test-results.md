# PresTag Power Measurement Log

**Append-only.** Each completed measurement gets one entry with a timestamp,
the conditions it was taken under, and the numbers. Never edit or delete an
entry — a measurement that turned out to be wrong gets a later entry saying so,
because the wrong ones are how the reasoning is reconstructed.

Design documents cite entries from here rather than restating figures, so a
number appears once and has provenance. The companion files are
[`power-test-plan.md`](power-test-plan.md) (procedure),
[`power-test-report.md`](power-test-report.md) (the report form) and
[`power-test-status.md`](power-test-status.md) (live handoff, overwritten).

## Format

```
### YYYY-MM-DD HH:MM  <short title>
- **build**: git hash, target, notable defines
- **conditions**: period, state, window, what was attached
- **result**: the numbers
- **notes**: anything that qualifies them
```

Rig, unless an entry says otherwise: PresTagv3 + PresTag on a baseboard,
Joulescope JS320 via `joulescope_server.py --use-server`, `charge/time` figure,
supply 2.485 V, monitor and Joulescope UI detached.

---

### 2026-09-08  Baseline, before any fix
- **build**: `411b046` PresTag, stock LPTIM stop-delay path, PA2/INT1 a floating input
- **result**:
  | condition | current |
  | --- | --- |
  | IDLE, clock set (3 × 300 s) | **0.2928 µA** (0.2921 / 0.2926 / 0.2937) |
  | CONFIGURED (2 × 900 s) | **0.5165 µA** — wakes every 60.0 s, 13.4 µC per wake |
  | RUNNING, 9 s period | **530.7 µA**, flat — `godown(STOP2)` is a no-op, never sleeps |
  | RUNNING, 10 s (600 s block) | **3.6948 µA** |
  | RUNNING, 60 s (3600 s block) | **0.8552 µA** |
- **notes**: two-point fit gave `I_rest` 0.287 µA, `Q_cycle` 34.1 µC, `T_knee` 119 s.
  Fit intercept and measured idle agree to 1.9%.

### 2026-09-08  Sample event anatomy
- **build**: `411b046` + phase probe
- **conditions**: 10 s period, 0.5 ms trace
- **result**: event 59 ms, **34.9 µC**; three `stopMilliseconds()` waits at
  495 / 143 / 165 µA; `ARROK` busy-wait 6.3–7.1 ms per call for 2 ms and 5 ms
  requests alike.

### 2026-09-08  Phase C and D
- **result**: C1 scheduled start held CONFIGURED, first sample **+17 s**;
  C3 stop **−3 s**, 29 of ~30 samples; C4 `tag-stop` → FINISHED, download
  clean; C5 hibernation entry at sample 120 (block boundary, **+510 s** after
  the window opened), exit **+66 s**; all Phase D value checks PASS.
- **notes**: C5 does not discriminate the §1.7 cursor fix — its window opened at
  t+690, so the first 60-boundary is sample 120, which is also a multiple of 120.

### 2026-09-09 ~11:00  Stop 2 sleep depth, all devices off
- **build**: `ef2f6db`~ + RTC Alarm A, PA2 still a floating input
- **conditions**: 2 s delay inserted before the LPS27 is powered and before the
  AT25 is woken, so nothing on the board draws
- **result**: **143 µA** (min 133). With the AT25 explicitly awake: **167 µA**.
  Shutdown baseline in the same trace **0.290 µA**.
- **notes**: the AT25 contributes only ~24 µA. This is the measurement that
  isolated the fault to the MCU domain rather than to any device.

### 2026-09-09 ~11:30  Stop-0-cause register capture
- **conditions**: captured at the `WFE`, run at 9 s, nothing attached
- **result**: all excluded — `SLEEPDEEP=1`, `LPMS=2`, `PWREN=1`, VOS Range 2,
  `ADEN`/`ADVREGEN`/`VREFEN`/`TSEN` all 0, `HSION=0`, `PLLON=0`,
  **`C_DEBUGEN=0`**, `NVIC ISPR[0..2]` all 0, `PWR_SR1=0`, `EXTI_PR1=0`.
- **notes**: every documented cause correctly excluded, and none was the cause.

### 2026-09-09 ~12:00  PA2/INT1 made analog
- **build**: `bf0c331`
- **result**: stop-delay plateau **143 µA → 8–16 µA**;
  10 s period with the LPTIM path **2.9742 µA**, `Q_cycle` **26.82 µC**.
- **notes**: floating CMOS input near mid-rail. Invisible in Run and impossible
  in Shutdown, so only Stop 2 ever showed it.

### 2026-09-09 ~12:10  RTC Alarm A stop-delay tick
- **build**: `0ac8bc6`
- **result**: 10 s period **1.7376 µA** (180 s window), `Q_cycle` **14.49 µC**;
  event 33 ms; waits 8–16 µA.
- **verification**: `stopMilliseconds(2000)` elapses **2013.7 ms** timed from the
  RTC calendar; alarm matches read from `RTC_SSR` are exactly **2 counts** apart,
  polled and slept alike.
- **notes**: an earlier claim that these delays ran 10× long was **wrong** — it
  came from a trace whose sample events were 100 s apart at a configured 10 s
  period, misread as 20 s delays.

### 2026-09-09 ~15:00  Regression after the Alarm A change
- **build**: `ef2f6db`
- **result**: C1 start **+64 s**, C3 stop **−6 s**, download PASS (988.75–988.94 hPa,
  25.09–25.25 °C); C5 hibernation entry sample 120 (**+510 s**), exit **+59 s**,
  165 samples / 3 headers, one 759 s gap, PASS.
- **notes**: matches the pre-change behaviour. Start offset +64 s is marginally
  over the ≤60 s the minute-alarm polling predicts; was +17 s before.

### 2026-09-09 ~16:00  Sweep, measured not extrapolated
- **build**: `ef2f6db`, PA2 analog, RTC Alarm A
- **conditions**: one full 60-sample block per point
- **result**:
  | period | window | current |
  | --- | --- | --- |
  | IDLE | 300 s | **0.2810 µA** |
  | 10 s | 600 s | **1.8099 µA** |
  | 60 s | 3600 s | *pending* |
  | 90 s | 5400 s | *pending* |

### 2026-09-09 ~17:25  60 s sweep point complete
- **build**: `ef2f6db`, PA2 analog, RTC Alarm A
- **conditions**: 3600 s window (one 60-sample block); one retry on first attach
- **result**: **0.5406 µA** at 2.4853 V

### 2026-09-09 ~17:25  Interim power model fit (two Shutdown points)
- **build**: `ef2f6db`
- **points used**: 10 s → 1.8099 µA, 60 s → 0.5406 µA
- **result** (`prestag_power_model.py --point 10:1.8099 --point 60:0.5406 --capacity 5.5 --capacity 11 --target-days 365`):

  | quantity | value | note |
  | --- | --- | --- |
  | `I_rest` | **0.2867 µA** | Shutdown floor |
  | `Q_cycle` | **15.23 µC** | charge per sampling cycle |
  | `T_knee` | **53.1 s** | sampling costs as much as resting |
  | max residual | 0.000 µA | exact — two-point fit has no residual check |
  | R² | 1.000 | |

  Predicted `I_avg` and lifetime (nominal upper bound):

  | period | `I_avg` | 5.5 mAh | 11 mAh | meets 365 d? |
  | --- | --- | --- | --- | --- |
  | 10 s | 1.810 µA | 127 d | 253 d | no |
  | 60 s | 0.541 µA | 424 d | 848 d | yes |
  | **90 s** | **0.456 µA** | **502 d** | **1005 d** | **yes** |
  | ∞ | 0.287 µA | 799 d | 1598 d | — |

- **notes**: two-point fit is exact by construction — `I_rest` and `Q_cycle` determined with no
  redundancy and no residual to check. The 90 s point (in flight) will be the first independent
  check; if `I_avg(90 s)` lands near 0.456 µA the fit stands. A miss of more than ~5% means one
  of the three points is wrong and the fit is to be distrusted.

### 2026-09-09 ~20:00  Measured sweep, three full blocks
- **build**: `890a11b` PresTag — PA2 analog, RTC Alarm A ticker
- **conditions**: one full 60-sample block per point, nothing attached
- **result**:
  | period | window | current |
  | --- | --- | --- |
  | IDLE | 300 s | **0.2810 µA** |
  | 10 s | 600 s | **1.8099 µA** |
  | 60 s | 3600 s | **0.5406 µA** |
  | 90 s | 5400 s | **0.4517 µA** |
- **fit**: `I_rest` **0.2842 µA**, `Q_cycle` **15.26 µC**, `T_knee` **53.7 s**,
  max residual **0.002 µA**, **R² = 0.999993**.
- **notes**: the linear model is now measured across a 9× span rather than
  assumed from one point. The fit intercept and the directly measured idle agree
  to 1.1% — two independent routes to the same quantity.
- **lifetime at the shipped 90 s period**: 5.5 mAh **505 days**, 11 mAh **1010
  days**, against one-year budgets of 0.628 and 1.256 µA. Both met; 5.5 mAh was
  21 days short before the PA2 and Alarm A fixes.

### 2026-09-09 ~20:30  PresTagRaw, first hardware run
- **build**: `890a11b` + `PresTagRaw/custom.h` aligned to PresTag
- **notes on what changed**: the variant had silently inherited `STANDBY` for
  all five states and the LPS27 driver defaults (10 ms power-up, up to six 15 ms
  polls) where PresTag uses `SHUTDOWN` and 5/5/1. Same board, so it already had
  the PA2 fix.
- **result**:
  | | PresTagRaw | PresTag |
  | --- | --- | --- |
  | IDLE, 300 s | **0.2792 µA** | 0.2810 µA |
  | RUN 10 s, 600 s block | **1.7746 µA** | 1.8099 µA |
  | download | **PASS** | PASS |
  | pressure | 985.13–985.56 hPa | 988.75–988.94 hPa |
  | temperature | 24.31–24.98 °C | 25.09–25.25 °C |
- **notes**: within 2% of PresTag on both currents, as expected once the configs
  match. 73 samples over 2 raw blocks, one header each, epochs monotonic. The
  raw export writes the same Pressure/Temperature/Voltage tables, so
  `prestag_check_download.py` needed no change. Pressure differs from PresTag's
  run by ~3.5 hPa because the runs are hours apart — real weather.
