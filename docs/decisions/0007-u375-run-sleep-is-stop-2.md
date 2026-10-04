---
type: decision
status: accepted
summary: IMUTagNandBmp581 sleeps between IMU wakes in Stop 2 rather than Stop 1, because Stop 1 current depends on code layout and Stop 2 is lower and stable.
---

# 0007. STM32U375 run-mode sleep is Stop 2, not Stop 1

Date: 2026-09-07 (commit e08e3fab)

The text below is cut verbatim from [Open Issues](../../embedded/tags/TODO.md); "the Standby
entry below" refers to [the Standby layout investigation](../../embedded/tags/common/core/design/investigations/2026-09-u375-standby-layout-dependence.md).

## Context

### RESOLVED by Stop 2: run-mode Stop 1 current is layout-dependent too

The same fault family as the Standby entry below, at the idle thread's sleep
during RUNNING, found on 2026-09-07 when run current rose from a remembered
548/815 uA at 100/400 Hz to 869/984.

**It is not a failure to sleep.** A probe in the retained SRAM2 page counted
what the idle hook actually did over a 37 s run at 100 Hz: the Stop path was
taken 1086 times, `LPMS=1`, `STOPF=1`, and `RTC_SSR` -- which runs in every
low-power mode -- measured **36.4 s of the 37 s asleep in Stop**, mean dwell
33 ms. The `stIsAlarmActive()` fallback to plain `SLEEP` fired 8460 times but
for 0.09 ms each, 0.75 s in total. The peripheral state latched at the instant
of Stop entry (SPI1, I2C1, all the RCC ENR/STPENR words, GPIO ODRs, ADC, LPTIM1,
`PWR_CR2`, `RCC_CR`, `CCIPR1`) was identical between builds.

**What varies is the current drawn while in Stop 1.** At 1 ms resolution the
whole distribution shifts, first percentile included (656 uA against 558), so
it is a raised sleeping floor and not more time awake.

| build (differs only in layout) | 100 Hz | 400 Hz |
| --- | --- | --- |
| without the page-skip commit | 671 | 811 |
| with it (HEAD) | 866 | 980 |
| HEAD + 8 or 16 inert `nop`s | 866 | -- |
| with an idle-branch probe compiled in | 670 | -- |
| with probe + register snapshot | 867 | -- |

Reproducible to 0.3 uA across three run cycles per build, and the probe builds
flipped both directions, which is what identifies it as layout rather than as
anything the page-skip code does -- that code cannot execute in a healthy run,
and `noinline` on it changed nothing.

## Evidence

**Stop 2 is not affected.** Same three layouts, 100 Hz: **605.0, 604.8, 604.9
uA** -- a 0.2 uA spread against 195. It is also lower at every rate, and
collection, timing and download pass at all of them:

| mode | 100 Hz | 400 Hz | 800 Hz | 1600 Hz |
| --- | --- | --- | --- | --- |
| Stop 1, best layout | 671 | 811 | -- | -- |
| Stop 1, bad layout | 866 | 980 | -- | -- |
| **Stop 2** | **605** | **746** | **959** | **1133** |

## Decision

The fix is `IMUTAG_RUN_SLEEP_MODE`, a per-target selector in the IMUTag
`state_run.c` defaulting to the historical `USE_STOP1` choice, with
IMUTagNandBmp581 setting `STOP2`. `USE_STOP1` stays 1 there because
`stopMilliseconds()` and the LPTIM1 trigger gate still read it. Other targets
are untouched.

## Consequences

**One caveat, recorded rather than dismissed.** The Stop 2 release check saw a
single `SetRtc failed: RTC sync failed while writing tag clock` in its 30 clock
cycles, where every Stop 1 gate that day was clean over 200. Stop 2 powers down
more than Stop 1, so this was chased: two dedicated 30-cycle runs on Stop 2
were clean (89/90 in total) and a Stop 1 control under the same rig conditions
was 30/30. That points at the known RV-3028 write intermittent rather than at
Stop 2, but the gate runs 30 clock cycles each time and will show a real rate
difference if there is one.

**The lesson for the release gate:** four consecutive `tag_release_check.py`
runs reported this run current and passed, because the gate bounds idle current
and only *reports* the run. Run current is what sets battery life during a
deployment, and it is now the second place a low-power mode has proved
layout-dependent. Bound it.
