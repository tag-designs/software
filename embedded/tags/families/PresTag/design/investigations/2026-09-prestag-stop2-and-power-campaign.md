---
type: investigation
status: closed
summary: The 2026-09-08/09 PresTag power campaign -- why Stop 2 looked broken (a floating PA2 input and the LPTIM arming cost), the bench incidents, and both executions' findings.
---

# PresTag Power Campaign and Stop 2 Diagnosis (2026-09)

Cut verbatim from [`../power-test-plan.md`](../power-test-plan.md) (sections
1.2a, 1.2c, 1.2e, 2.0a, 10, 11 and 12) and from the closing handoff in
[`../power-test-status.md`](../power-test-status.md). Covers 2026-09-08
(first execution at `411b046`) through 2026-09-09 (second execution at
`890a11b`, merged at `e0362fc`). Outcome: Stop 2 works once PA2/INT1 is analog
(`bf0c331`) and stop delays come from a free-running RTC Alarm A
(`0ac8bc6`, see [the decision record](../../../../../../docs/decisions/0009-prestag-stop-delay-rtc-alarm-a.md)); both cells clear a year
at the 90 s default. Section numbers (§) refer to the test plan; the log-cursor
fix is in [`2026-09-prestag-log-cursor-round-up.md`](2026-09-prestag-log-cursor-round-up.md).
The measurements themselves are in
[`../power-test-results.md`](../power-test-results.md).

## From the test plan, sections 1, 2 and 10

### 1.2a Measured: the Stop 2 path does not sleep at all

The sleep-mode switch above is not a choice between two working modes. Measured
on a PresTagv3 at `411b046`:

| Period | Mode requested | Measured `I_avg` |
| --- | --- | --- |
| 9 s | `STOP2` | **530.7 µA** |
| 10 s | `SHUTDOWN` | **3.695 µA** |

A 144x step for a one-second change in period. A 0.5 s-resolution trace at 9 s is
**flat at 530.7 µA** — minimum 530.62, with the sampling events visible only as
536 µA bumps every 9 s. There is no low-current interval between samples at all,
which is the signature of a part that never entered a low-power mode rather than
one whose low-power mode is expensive.

The cause is in `godown()`
([pwr.c:234](../../../../common/core/src/pwr.c#L234)):

```c
void godown(enum Sleep sleepmode)
{
  tagPowerEnterTerminalSleep(sleepmode);
}
```

`tagPowerEnterTerminalSleep()` on this part selects `LPMS` only for Standby and
Shutdown, and returns without touching anything for any other request
([pwr-l432.c:33-45](../../../../common/core/src/pwr-l432.c#L33-L45)). **A `STOP2`
return from `Running()` is therefore a silent no-op**: the state handler asks to
sleep, `godown()` declines without saying so, and the main loop goes round
again.

Consequences:

- Sub-10 s periods are unusable, and not because Stop 2 is costly — it is never
  entered. An L432 in Stop 2 should be nearer 1-2 µA.
- **The 9 s / 10 s pair does not measure the cost of the per-sample reboot**,
  which is what it was designed for. That comparison needs a working Stop 2
  first.
- Field periods are unaffected: they are at or above 10 s, take the Shutdown
  path, and measure 3.695 µA at 10 s and 0.855 µA at 60 s.

This is separate from `stopMilliseconds()`, which enters Stop through its own
LPTIM1 path rather than through `godown()`.

### 1.2c Resolved: a floating input, and the LPTIM arming cost

Stop 2 works. Two independent faults made it look otherwise, and both are fixed.

**A floating input.** PA2/INT1 is unused and was configured as a digital input
with nothing driving it, so it sat near mid-rail and dissipated continuously.
Configured as analog
([board-customizations.json](../../../../../boards/PresTagv3/cfg/board-customizations.json)),
the stop-delay plateau fell from **143 µA to 8-16 µA** with the LPS27 off and
the AT25 in deep power-down.

It hid because it cost nothing anywhere convenient: negligible against Run
current, and impossible in Shutdown, where VCORE is removed -- which is why
idle always measured a clean 0.29 µA. It showed only in Stop 2, the mode used
for every driver delay.

The LPTIM arming cost and its replacement: see [the decision record](../../../../../../docs/decisions/0009-prestag-stop-delay-rtc-alarm-a.md).

**How not to look for this.** Every documented cause was excluded by capturing
registers at the `WFI` -- `SLEEPDEEP`, `LPMS`, `PWREN`, voltage range, ADC and
`VREFINT`, RCC clock requests, `C_DEBUGEN`, pending interrupts and wakeup flags
were all correct or clear. They were excluded correctly and none was the cause.
Meanwhile an early test *appeared* to exclude pins and did not: it called
`tagDevicesApplyStandbyPins()`, which writes only `PWR->PUCRx`/`PDCRx` -- the
Standby and Shutdown pull configuration, applied through `PWR_CR3_APC`, with no
effect in Stop mode, where GPIOs keep their Run configuration. A null result
from a test that cannot detect the fault is not an exclusion, and recording it
as one cost hours.

### 1.2e Defect found on the way: a stale stored configuration

`tag-start` printed `period: 10`; the tag ran at 9 s (`RTC_WUTR = 8`, bursts
8.97 s apart) with `sconfig.lps_period = 9` still in flash. `writeStoredConfig()`
programs `sconfig` without checking `FLASH_Program_Array()`'s result, and
`erasePersistent()` never checks that its erase happened; on an L4 a program
into a non-erased double word is refused, so the previous configuration
silently survives a reset-and-start. Every "10 s" run between the first POR and
an explicit page erase was a 9 s run — including one bisect that seemed to show
Shutdown broken and was in fact the Stop 2 path behaving as documented. Two
lessons for the plan: **verify the period from the data** (burst spacing, or
the download's epoch step), not from the host's echo; and treat `tag-start`'s
configuration print as the request, not the result.

### 2.0a If the tag is found at 14 mA, it is in the system bootloader

Observed twice during the first execution of this plan: the tag ended up running
the **STM32 system-memory bootloader** — PC in `0x1FFF....`, `HSI16` as
SYSCLK, ~14 mA, RTC registers reading zero — and from that state **no reset
recovered it**: five system resets and two hardware reset pulses re-entered the
ROM every time, while flash was intact, `PEMPTY` was clear and the option bytes
were normal. Two things did recover it: a debugger jump,

```sh
STM32_Programmer_CLI -c port=SWD mode=UR reset=HWrst -g 0x08000000
```

and a power-on reset (an accidental DUT power loss booted flash cleanly).

The first entry followed a self-reset during a run of an instrumented image; the
second followed the download target's post-program `-rst`, which had been
substituted for the original `-g 0x08000000` during this work. The original jump
is restored: it starts the firmware without consulting boot selection at all.
BOOT0 is tied on this board and hundreds of tags have deployed, so this is an
unfortunate reachable state that a power cycle clears, not a board fault; the
latch mechanism was not established here. The practical rule is the recovery
above, and to keep `-g` in the download target.

### Still open, to confirm not fix

This plan changes no further firmware. Two places where code and comments
disagree remain; each should be filed once observed:

1. **`ALARM_HOUR` behaves as `ALARM_MINUTE`** (§1.6) — identical masks in
   `enableAlarm()`. Hibernation wakes 60× more often than the comment claims.
   Confirmed or refuted by **H3**.
2. **`start_delay` is silently ignored by this family** (§1.4), so
   `tag-start --start-now` has no effect on a PresTag. Confirmed by **C2**.

## 11. Findings from the first execution (2026-09-08, `411b046` + probes)

Measured on a PresTagv3 at 2.485 V, Joulescope JS320, auto range, `charge/time`.

| Measurement | Result |
| --- | --- |
| A1 IDLE, clock set (300 s ×3) | **0.2928 µA** (0.2921 / 0.2926 / 0.2937) — PASS |
| A2 CONFIGURED (900 s ×2) | **0.5165 µA**; wakes every 60.0 s (trace), **13.4 µC per wake** |
| B2 9 s (Stop 2 path) | **530.7 µA, flat** — never sleeps; `godown(STOP2)` is a no-op (§1.2a) |
| B3 10 s | **3.6948 µA** |
| B7 60 s | **0.8552 µA** (predicted 0.860 from B3 + A1) |
| Fit, Shutdown regime | `I_rest` 0.287 µA, `Q_cycle` 34.1 µC, `T_knee` 119 s |
| Predicted 90 s default | 0.666 µA → **11 mAh 688 d, 5.5 mAh 344 d** (nominal) |
| Download check, 9 s run | PASS: 990.56–990.94 hPa, 25.8–26.1 °C, no sentinels |
| One sample event (10 s trace) | 59 ms, 34.1 µC; 9 s event 52 ms — **boot ≈ 7 ms ≈ 4.5 µC** |
| Sample path (probe) | 28.9 ms loop-top to loop-top; three `stopMilliseconds` ≈ 6.7 ms each |
| 4-byte flash write | `WIP` clear on first poll: **0.5 ms** — cheap, as the datasheet says |

Sub-1 µA at 60 s and one year on 11 mAh are confirmed; 5.5 mAh is 21 days short
before derating, and a ~4.4 µC cut in `Q_cycle` would carry it over.

### Where the 34 µC goes, and what it says about optimisation

- **~20 ms of `stopMilliseconds()` at Run/Sleep current** (§1.2b, §1.2c) — the
  largest controllable item. The waits do elapse, but shallow: 140 µA measured
  with all devices off, against 1-2 µA for Stop 2. Making them sleep properly is
  worth roughly 10 µC and is what takes 5.5 mAh across a year. An LSI kernel
  clock was tried and recovered only 3.9 µC (§1.2c).
- **Boot from Shutdown, ~7 ms, 4.5 µC** — inherent to Shutdown-per-sample.
  Avoidable only by a working Stop 2 between samples, which `godown()` does not
  implement on this part (§1.2a).
- **Flash write is not the cost.** Byte programming is ~30 µs; batching samples
  into pages would save little and, on a small cell, risk a brownout mid-page.
- **SPI is polled peripheral SPI at 1 MHz**, ~8 µs per byte; DMA SPI would buy
  back essentially nothing.
- **LPS27 timing is already tuned** (5 / 5 / 1 versus driver defaults 10 / 15 /
  6), though `PresTagRaw` never received it.
- The `stopMilliseconds()` synchronisation cost means the AT25 recharge wait is
  longer than requested but spent at Run current; on a cell the metering itself
  is satisfied, the energy is what is lost.

### Trace features that are not the firmware

After the sample pass the trace shows a ~7 ms plateau at ~160 µA and a 2.5 mA,
1.5 ms spike before the current settles. Forty consecutive loop passes after
every sample measure 0.9–1.0 ms each with the ADC running (533 µA elsewhere),
there are no threads besides main and idle, and no ISRs beyond the RTC. The
supply current cannot fall below what the MCU is drawing, and the dip and spike
carry nearly equal and opposite charge (−2.6 µC, +3.0 µC). The working
conclusion is an **instrument auto-range transition** with charge conserved but
the shape distorted — so `Q_cycle` from `charge/time` stands, but per-phase
attribution of that tail does not. A fixed-range capture would settle it; the
instrument wedged before one could be taken.

### Not yet run, as of the first execution

The LSI delay experiment (§1.2c) needs its all-devices-off wait to settle sleep depth.

A3, A4, A5; B1, B4, B5, B6; all of Phase C (C1–C6, T4); H3 for hibernation. The
CONFIGURED minute alarm was confirmed by trace; the HIBERNATING one was not
measured.

**All of these except B1, B4, B5 and T4 were run on 2026-09-09** — see §12.

### Tooling found wanting, and fixed or reverted

- `joulescope_measure.py` client timeout fixed at 300 s; scaled to the window.
- `joulescope_server.py` died on `BrokenPipe` because `flush()` sat outside
  the guard; moved inside.
- Download target: `-rst` briefly replaced `-g 0x08000000`; the tag was found in
  the ROM immediately afterwards (§2.0a). Reverted to the jump; `reset=HWrst`
  and a 4-try connect retry kept.
- The instrument wedges after repeated direct open/close cycles; use the server
  for the whole session and do not run direct-driver captures alongside it.

## 12. Second execution (2026-09-09, `890a11b`)

The first execution's numbers stand as a record of the *pre-fix* firmware. Two
faults were found and fixed between the two, and every number below moved
because of them:

- **PA2/INT1 was a floating digital input** on the PresTagv3 board, dissipating
  ~130 µA whenever the part was in Stop 2 — invisible in Run, and impossible in
  Shutdown where VCORE is removed, which is why it hid for so long. Made analog
  in `bf0c331`.
- **`stopMilliseconds()` re-armed LPTIM per delay**, and the `ARROK` busy-wait
  costs 6.3–7.1 ms of Run current each time at a 1024 Hz LSE. Replaced by a
  free-running **RTC Alarm A** tick set up on RUNNING entry, in `0ac8bc6`.

`Q_cycle` fell **34.06 → 26.82 → 15.26 µC** across the two fixes.

| Measurement | First execution | Second execution |
| --- | --- | --- |
| IDLE | 0.2928 µA | 0.2810 µA (A1′) |
| FINISHED | not run | **0.2790 µA** |
| HIBERNATING | not run | **0.3769 µA**, 5.05 µC per wake |
| 10 s | 3.6948 µA | **1.8099 µA** |
| 60 s | 0.8552 µA | **0.5406 µA** |
| **90 s (default)** | 0.666 µA *predicted* | **0.4517 µA measured** |
| Fit | `I_rest` 0.287, `Q_cycle` 34.1, `T_knee` 119 s | `I_rest` **0.2842 µA**, `Q_cycle` **15.26 µC**, `T_knee` **53.7 s**, R² 0.999993 |
| 5.5 mAh at 90 s | 344 d — **21 days short of a year** | **505 d — clears it** |
| 11 mAh at 90 s | 688 d | **1010 d** |

Both cells now clear a year at the shipped period, which was the object of the
exercise. The model is measured at three periods, not extrapolated from one.

Schedule behaviour was re-run in full against the Alarm A change and is
unchanged: C1–C5 as before, **C6a** (scheduled stop at 90 s) FINISHED at the
stop epoch +1 s, **C6b** (commanded stop at 90 s) confirmed in one poll with an
immediate clean download. **H3** counted the hibernation wakes directly — five
in 295 s at exactly 60.0 s, twice over — settling §1.6: the hour alarm does
behave as a minute alarm. That same run was configured to open its hibernate
window between samples 60 and 120, which is what C5 could not do, and so it is
also the hardware confirmation of the §1.7 cursor fix.

`PresTagRaw` was brought into line with PresTag (`4527184` — it had silently
inherited `STANDBY` for every state and the LPS27 driver defaults) and then
measured: IDLE 0.2792 µA, 10 s run 1.7746 µA, download PASS. Within 2% of
PresTag on both, as it should be once the configurations match.

**Still outstanding:** T4 (brownout recovery) needs a genuine brownout and has
not been run, and F3 — `writeStoredConfig()` ignoring the flash programming
status, so a stale configuration can survive a reset-and-start — is open.

## Handoff at the close of the campaign

From [`../power-test-status.md`](../power-test-status.md), as last written.

Updated: **2026-09-09 ~22:00**  ·  Merged to `main` at `e0362fc`

### Current objective

**None in flight — the campaign is complete and the rig is idle.** The operator
is away for 12 days from 2026-09-09. Pick up from "Outstanding" below.

### State right now

| | |
| --- | --- |
| worktree | clean, merged to `main` |
| tag firmware | `890a11b` PresTag — PA2 analog, RTC Alarm A ticker |
| tag state | **IDLE**, RTC set, no configuration programmed |
| instrument | `joulescope_server.py` **stopped**, released cleanly, DUT left powered (2.47 V, `Device ID 0x435` answers under reset) |
| in flight | nothing |

### Done

- **Stop 2 works.** Two independent faults, both fixed: PA2/INT1 was a floating
  digital input (~130 µA, visible only in Stop 2), and the LPTIM re-arm cost
  6.3–7.1 ms of Run current per delay. `Q_cycle` 34.06 → 26.82 → **15.26 µC**.
- **Power model measured, not extrapolated**, at three periods on the shipping
  build:

  | state / period | current |
  | --- | --- |
  | IDLE | 0.2810 µA (A1′), 0.2928 (A1), 0.2860 (A5) |
  | FINISHED | **0.2790 µA** |
  | CONFIGURED | 0.5165 µA (13.4 µC per minute wake) |
  | HIBERNATING | **0.3769 µA** (5.05 µC per minute wake) |
  | 10 s | **1.8099 µA** |
  | 60 s | **0.5406 µA** |
  | **90 s (default)** | **0.4517 µA** |

  Fit: `I_rest` **0.2842 µA**, `Q_cycle` **15.26 µC**, `T_knee` **53.7 s**,
  R² 0.999993, worst residual 0.4%.
- **Both cells clear a year at 90 s**: 5.5 mAh **505 d**, 11 mAh **1010 d**
  (379 / 758 d at a 75% derating). 5.5 mAh was 21 days short before the fixes.
- **Phases A, B, C, D all pass.** C6 (scheduled and commanded stop at the 90 s
  default) added: FINISHED at the stop epoch **+1 s**, and `tag-stop` confirmed
  in one poll with an immediate clean download.
- **H3 settled §1.6**: five hibernation wakes in 295 s at exactly 60.0 s, twice
  over. The hour alarm behaves as a minute alarm.
- **§1.7 cursor fix confirmed on hardware.** H3's run opened its hibernate window
  between samples 60 and 120 — the case C5 could not discriminate — and entry
  was at sample 60, not 120.
- **PresTagRaw** aligned with PresTag (`4527184`) and measured: IDLE 0.2792 µA,
  10 s run 1.7746 µA, download PASS. Within 2% of PresTag on both.
