---
type: procedure
status: current
summary: PresTag power and schedule test plan -- what the firmware does in each state, the configs, the resting-state, period-sweep, schedule and data phases, the power model and the gates.
---

# PresTag Power and Schedule Test Plan

A PresTag (PresTagv3 board, STM32L432) rests in **Shutdown** in every state.
In `RUNNING` it wakes once per sample period and reboots each time. Average
current follows `I_avg(T) = I_rest + Q_cycle / T` at periods of 10 s and
longer. The measured fit is `I_rest` 0.284 µA, `Q_cycle` 15.26 µC, and
0.4517 µA at the shipped 90 s period. At that period a 5.5 mAh cell lasts 505
days and an 11 mAh cell 1010 days ([results](power-results.md)).

This plan covers four things: **sample period**, **scheduled start**, **stop**
and **hibernation**. It separates two questions:

- *How much current does each state draw?* The period sweep and the
  resting-state measurements answer this. They need whole repeating units of
  the sampling cycle to be unbiased.
- *Does the schedule do what it was told?* The transition tests answer this.
  They need only one representative period.

Start, stop and hibernation do not depend on the period, so they are tested
once, at a single period. The rig, the Joulescope server, flashing, windows,
supply voltage and what to record are in the shared
[power testing procedure](../../../../../docs/bench/power-testing.md). Open
work is in [`../TODO.md`](../TODO.md). The history is in two investigations:
how the power figures were reached is in
[the campaign investigation](investigations/2026-09-prestag-stop2-and-power-campaign.md),
and the log-cursor fix is in
[the cursor investigation](investigations/2026-09-prestag-log-cursor-round-up.md).

## 1. What the firmware does

### 1.1 Sample period

`Config.period` (protobuf) becomes `sconfig.lps_period`, in **seconds**.
`Running(T_INIT)` arms the RTC periodic wakeup with `enableTicker()`
([state_run.c](../src/state_run.c)). The wakeup register is 16 bits wide, so
the usable range is **1 s to 65536 s**. `writeConfig()`
([config.c](../src/config.c)) rejects a configuration whose `period` is zero,
and accepts any configuration only while the tag is `IDLE`.

### 1.2 The sleep mode changes at 10 seconds

The last statement of `Running()` picks the sleep mode between samples:

```c
if (sconfig.lps_period < 10)
  return STOP2;
else
  return PRESTAG_RUNNING_LONG_SLEEP_MODE;   /* SHUTDOWN on PresTag */
```

At 10 s and above the MCU enters **Shutdown**, so it **reboots for every
sample**. SRAM is lost, `crt0` runs, the clock tree and devices are set up
again, and the run state is recovered from the RTC backup registers through
`pState`. `PresTag/inc/custom.h` sets every other state to Shutdown as well.

Below 10 s `Running()` asks for Stop 2. On the L432, `godown()` turns that
into a silent no-op: `tagPowerEnterTerminalSleep()` handles only Standby and
Shutdown. So a sub-10 s period never sleeps at all, and measured 530.7 µA flat
(F2 in the [TODO](../TODO.md)). **Periods below 10 s are a bench
configuration**, and nothing is deployed in that regime.

### 1.3 The stop-delay tick: RTC Alarm A

Every driver wait goes through `stopMilliseconds()`. It counts matches of a
free-running RTC Alarm A, which `Running()` sets up once on entry and the
`disableAllAlarms()` of whichever state comes next tears down. Why it is
Alarm A and not LPTIM1:
[decision 0009](../../../../../docs/decisions/0009-prestag-stop-delay-rtc-alarm-a.md).

Nothing on this path waits on synchronisation. The flag is set in the RTCCLK
domain and read directly. RM0394 specifies that `ALRAF` clears **2 APB
cycles** after writing 0, about 100 ns. LPTIM lacks that property, and it is
why this works.

`MASKSS = 1` matches every 2 sub-second counts: **1.95 ms**, the finest
available at `PREDIV_S = 1023`. `MASKSS = 0` is not finer; it turns off the
sub-second comparison and fires once a second.

Two hazards:

- **ChibiOS owns EXTI IMR1 line 18** for Alarm B. Arming `ALRAIE` without
  masking the interrupt there runs the full RTC alarm ISR every 1.95 ms instead
  of waking silently. That held the tag out of Shutdown entirely, at 188 µA.
- **Stop 2 halts TIM2**, the ChibiOS tick, so a pending virtual timer would
  never fire. Delays fall back to the RTOS sleep when a timer is armed, when a
  monitor session is open, or outside a run.

Checked against the RTC calendar, which keeps running through Stop 2:
`stopMilliseconds(2000)` takes **2013.7 ms**, and alarm matches read from
`RTC_SSR` are exactly 2 counts apart, whether polled or slept through.

### 1.4 The repeating unit is 60 samples, not one

Two different flash writes happen at two different rates:

| Event | Cadence | Cost |
| --- | --- | --- |
| External flash sample write (`writeDataLog`) | **every sample** | AT25 wake, 4-byte program, sleep |
| Internal flash header (`writeDataHeader`) | **every 60 samples** (`DATALOG_SAMPLES`) | internal flash unlock/program/lock, plus `stopMilliseconds(2)` |

So the energy cycle repeats every `60 × period` seconds. **A window that is not
a whole number of 60-sample blocks is biased**: it includes or leaves out a
header write that the average should have spread over 60 samples. A fresh run
after `tag-reset` starts at `external_blocks == 0`, so header writes land at
samples 0, 60, 120, and so on.

### 1.5 The log invariant

The download pairs headers and pages by position. Block `index` takes its
header from `vddHeader[index]` and its samples from byte offset
`index × sizeof(t_DataLog)` ([datalog.c](../src/datalog.c)). So the invariant
is `external_blocks == pages × DATALOG_SAMPLES`. `restoreLog()` establishes it
after a reset, and `Running(T_INIT)` keeps it by rounding the cursor up to a
whole page. A sample that is not where its slot puts it -- because the core was
halted, a wakeup was lost or the clock was set -- also starts a new page. Both
round-ups use `DATALOG_SAMPLES`. Using twice that value once desynchronised the
log after a brownout
([cursor investigation](investigations/2026-09-prestag-log-cursor-round-up.md)),
and **T4** is the regression test.

Restart recovery is otherwise harmless on a PresTag. The LPS27 is read one-shot
per sample, with no FIFO phase or watermark to resynchronise, so the log cursor
is the only thing recovery has to get right.

### 1.6 Start: absolute epochs, polled once a minute

`writeConfig()` takes the start time straight from
`active_interval.start_epoch` and never reads `config->start_delay`. That field
belongs to IMUTag. So:

- **`tag-start --start-now` does nothing on a PresTag.** To start immediately,
  set `active_interval.start_epoch` to zero or to a past epoch.
- A scheduled start can only be given as an absolute `start_epoch`.

PresTag does not define `TAG_CONFIGURED_IMMEDIATE_START`. A start command
therefore lands in `CONFIGURED`, which arms the minute alarm and sleeps in
Shutdown, waking once a minute to compare `timestamp >= sconfig.start`.
**Allow up to ~60 s from a due start to `RUNNING`.** In practice `tag-start`
has reported `RUNNING` immediately for a `start_epoch` of 0
([results](power-results.md), C2). `CONFIGURED` is a real low-power waiting
state: a configured tag sits in it until deployment.

### 1.7 Stop

- **Scheduled**: `Running()` checks `sconfig.stop < timestamp` on each wakeup
  and goes to `Finished`. The latency is up to one sample period.
- **Commanded**: `tag-stop` posts a work bit, and the state machine acts later.
  `tag-stop` polls for the state, bounded by `--settle-timeout`. See
  [verifying firmware](../../../../../docs/bench/verifying-firmware.md#5-host-tools-an-acknowledgement-is-not-a-completion).

### 1.8 Hibernation

**Entry** is checked in `Running()` on each sample. It requires the epoch to be
inside a hibernate window **and** `external_blocks % DATALOG_SAMPLES == 0`. So
there is one entry opportunity per header block, every `60 × period` seconds:
every 10 minutes at a 10 s period, every 90 minutes at the 90 s default. **A
hibernation window shorter than `60 × period` may contain no entry opportunity
and be skipped.** Entry happens at the first block boundary at or after the
window opens, not at the instant it opens.

**Exit**: `Hibernating(T_INIT)` arms `enableAlarm(1, ALARM_HOUR)`. Since
`060a566` that alarm compares minutes and seconds, so it fires **once an
hour, on the hour**. Hibernation therefore costs one wake an hour, and the tag
returns to `RUNNING` up to about an hour after the window closes. The recorded
hibernation figures (0.3769 µA, wakes every 60.0 s, exit within ~60 s) were
measured before that change. Re-measuring them is in the
[TODO](../TODO.md).

At most two hibernate windows are stored (`hibernate[2]` in `t_storedconfig`).

## 2. Measuring this tag

The shared procedure applies unchanged. What is specific to PresTag:

- **Dynamic range.** In `RUNNING` the tag sits at a few hundred nA and jumps to
  milliamps for ~10 ms per sample. At 90 s that is a duty cycle of about 1 in
  10,000, so auto-ranging and `charge/time` are not optional.
- **Windows.** The run window is a whole number of 60-sample blocks (§1.4).
  Its starting phase does not matter, only its length. Wait three sample
  periods or 60 s after `RUNNING`, whichever is longer. `CONFIGURED`
  windows are whole minutes, at least five. `HIBERNATING` windows are now
  whole hours.
- **Cross-check the floor two ways.** The fit intercept `I_rest` (§4) and the
  measured `IDLE` current (§3) are independent routes to the same quantity.
  If they disagree, distrust the fit first: a single bad point moves the
  intercept a long way.
- **Flash only after `tag-stop` and `tag-reset`**, and keep `-g 0x08000000` in
  the download target. Both are explained in the shared procedure.
- **Check the period from the data** -- burst spacing, or the download's epoch
  step. Do not trust the host's echo (F3 in the [TODO](../TODO.md)).
- The U375 Standby and Stop 3 findings do not apply to this L432 part, which
  has a working Shutdown. The discipline does carry over: measure after every
  firmware change.

## 3. Configuration

`tag-start -c FILE` without `--merge` programs exactly what the file says.
Field names are protobuf-JSON, matching
[`host/docs/fixtures/qtmonitor/prestag.json`](../../../../../host/docs/fixtures/qtmonitor/prestag.json):

```json
{
  "tag_type": "PRESTAG",
  "active_interval": { "start_epoch": 0, "end_epoch": 2147483647 },
  "hibernate": [
    { "start_epoch": 0, "end_epoch": 0 },
    { "start_epoch": 0, "end_epoch": 0 }
  ],
  "period": 10
}
```

[`prestag_mkconfig.py`](../../../../tools/prestag_mkconfig.py) turns offsets
such as "start five minutes out, hibernate from +25 to +45 minutes" into
absolute epochs, and prints the epochs it chose. It refuses a zero period and a
third hibernation window:

```sh
embedded/tools/prestag_mkconfig.py --period 10 --out /tmp/p10.json
embedded/tools/prestag_mkconfig.py --period 10 --run-for 5400 \
    --hibernate 1500:2700 --out /tmp/hib.json
```

Shipped defaults:
[`PresTag`](../../../../proto-c/prestag-proto-c/default-config.json) uses
`period: 90`, and
[`PresTagRaw`](../../../../proto-c/prestagraw-proto-c/default-config.json) uses
`period: 60`. Check which target is on the bench before quoting "the default".

## 4. Phases

### Phase A — resting states

`IDLE` and `FINISHED` are flat. `CONFIGURED` wakes once a minute and
`HIBERNATING` once an hour.

| ID | State | How to reach it | Window | Repeats |
| --- | --- | --- | --- | --- |
| A1 | `IDLE`, clock set | `tag-reset --set-rtc` | 300 s | 3 |
| A2 | `CONFIGURED` | start with `start_epoch = now + 3600` | 900 s | 2 |
| A3 | `FINISHED` | run briefly, then `tag-stop` | 300 s | 3 |
| A4 | `HIBERNATING` | inside the C5 window | whole hours | 2 |
| A5 | `IDLE` again | `tag-reset --set-rtc` after download | 300 s | 3 |

A1 and A5 are the same logical state reached by two histories. Comparing them
is the point, and a divergence is a finding even when both numbers look
plausible.

**Gate: `IDLE` and `FINISHED` below 1 µA.** The floor is the L432 in Shutdown,
plus the RV3028, the LPS27 powered down and the AT25 in deep power-down: a few
hundred nA. At or above 1 µA, something is still powered or a pin is driven
against a pull-up. Do not borrow `tag_lifecycle_check.py`'s default
`--idle-max-ua` of 100 µA; a PresTag at 50 µA would pass it while being two
orders of magnitude out. `CONFIGURED` and `HIBERNATING` are recorded, not
gated. Their excess over `IDLE`, multiplied by the wake interval, is the charge
per wake.

### Phase B — period sweep

| ID | Period | Regime | Window (1 block) | Field-relevant |
| --- | --- | --- | --- | --- |
| B1 | 1 s | Stop 2 request | 60 s | bench only |
| B2 | 9 s | Stop 2 request | 540 s | bench only |
| B3 | 10 s | Shutdown | 600 s | yes |
| B4 | 15 s | Shutdown | 900 s | yes |
| B5 | 30 s | Shutdown | 1800 s | yes |
| B6 | 90 s | Shutdown | 5400 s | **the shipped default** |
| B7 | 60 s | Shutdown | 3600 s | extra fit leverage |

Each point is a full cycle, because the config can be written only in `IDLE`
and a fresh erase keeps block boundaries predictable:

```sh
build-host/bin/tag-reset --set-rtc          # stop, erase, IDLE, clock set
build-host/bin/tag-start -c /tmp/p10.json   # -> CONFIGURED -> RUNNING (≤60 s)
# confirm RUNNING, wait the start transient, then one block:
embedded/tools/joulescope_measure.py --use-server --duration 600 --window 0.5 --repeat 2
```

Do not pass `--start-now` (§1.6).

**The model.** At 10 s and above, the tag sits in Shutdown between samples,
and every sample costs the same packet of charge: wake, reboot, sensor read,
external-flash write. So:

```
I_avg(T)  =  I_rest  +  Q_cycle / T                    [µA, with Q in µC, T in s]

            where  Q_cycle = Q_sample + Q_header / 60
```

`I_rest` is the Shutdown floor. `Q_cycle` is the charge per sample, including
the header write spread over its 60 samples. Fit the Shutdown points by least
squares on `(1/T, I_avg)`: the intercept is `I_rest` and the slope is
`Q_cycle`. The law saturates. At the knee, `T_knee = Q_cycle / I_rest`,
sampling costs as much as resting. Well above the knee, doubling the period
buys almost nothing. The law does not extend below 10 s (§1.2).

```sh
embedded/tools/prestag_power_model.py \
    --point 10:<uA> --point 60:<uA> --point 90:<uA> \
    --capacity 5.5 --capacity 11 --target-days 365
```

The script prints `I_rest`, `Q_cycle`, `T_knee`, the residuals, and a lifetime
table for each capacity. A poor straight-line fit means a measurement is wrong
or the regime assumption has broken.

**Sanity scale.** A healthy PresTag averages under 1 µA at 60 s, and the whole
field range is roughly 0.5 µA to 4 µA. A sweep point in the tens of µA, or a fit
that returns hundreds of µC, is a broken measurement. The usual causes are a
monitor still attached or a pin driven against a pull-up; a slow tag is not
among them.

**Lifetime.** `lifetime (days) = 1000 × C / (24 × I_avg)`, with C in mAh and
I_avg in µA. A year needs **0.628 µA** on a 5.5 mAh cell and **1.256 µA** on
11 mAh. The formula is an upper bound, so derate it before quoting a
deployment:

- self-discharge is comparable to the load at these currents;
- usable capacity ends at a cutoff voltage;
- cold reduces capacity;
- the per-sample peak sags a small, high-impedance cell near end of life.

Record the fit and the derated estimate separately.

### Phase C — schedule behaviour, at `T = 10 s`

10 s is the shortest field period. It gives a header block every 10 minutes.

| ID | Test | Expected |
| --- | --- | --- |
| C1 | Scheduled start: `start_epoch = now + 300`; confirm `CONFIGURED`; measure 300 s (A2); poll from just before the epoch | `CONFIGURED` until the epoch, `RUNNING` ≤ ~60 s after it. An immediate `RUNNING` means the schedule was not programmed -- most likely a `--merge` |
| C2 | Immediate start: `start_epoch = 0` | `RUNNING` within ~60 s |
| C3 | Scheduled stop: `end_epoch = now + 900`, left detached | `FINISHED` within one period of the epoch; then measure A3 |
| C4 | Commanded stop: `tag-stop`, then **download immediately** | `tag-stop` exits 0 and `FINISHED` is observed. No "Can't dump logs from current state" |
| C5 | Hibernation: period 10, `end_epoch = now + 5400`, `hibernate[0] = { now + 1500, now + 2700 }` | Entry at the first 60-sample boundary at or after the window opens, not at the instant it opens. To check the cursor fix, open the window *between* samples 60 and 120, so the fixed gate enters at sample 60 and the old one would wait for sample 120. Exit after the window closes, within the hourly alarm (§1.8). `FINISHED` at `end_epoch` |
| C6 | Repeat C3 and C4 at `T = 90 s` | Nothing in the stop path depends on the period |
| H3 | Hibernation wake cadence: trace inside the window | One wake per hour, on the hour (§1.8). A wake every minute would mean the `060a566` fix is not in the image |
| T4 | Brownout recovery: run until an **odd** number of headers is written, stop, induce a genuine brownout, run two more blocks, download | Continuous data with only the partial page lost, and no zero-sample block. A regression shows as one block with a header and zero samples at the pre-brownout page count, and every later block displaced by one. A probe reset takes the `T_CONT` path and does not test this; if a brownout cannot be induced, record that T4 did not run |

Each attach connects under reset, so poll only around expected transitions and
record every attach. Prefer reading the epochs back from the download.

### Phase D — data verification

Every Phase C run ends with a download, checked for **values** as well as
structure:

```sh
build-host/bin/tag-dwnld -o /tmp/prestag-<id>.db
embedded/tools/prestag_check_download.py /tmp/prestag-<id>.db \
    --period 10 --expect-duration 5400 --expect-gaps 1
```

[`prestag_check_download.py`](../../../../tools/prestag_check_download.py)
reads `Pressure(Epoch, Pressure)` in hPa, `Temperature(Epoch, Temperature)` in
°C and `Voltage(Epoch, Voltage)` in volts
([pressure.cc](../../../../../host/libraries/tagcore/sqlitelog/pressure.cc)).
It checks:

- the sample count against duration ÷ period;
- that epochs are monotonic;
- that spacing is exactly one period, with each larger step reported as a gap
  (`--expect-gaps 1` for C5, 0 otherwise);
- one header per 60 samples. A mismatch is the cursor-desynchronisation
  signature.

Structure alone passes on well-formed nonsense:

- **A failed sensor read is logged, not dropped.** `lps27GetPressureTemp()`
  presets its outputs to `SHRT_MIN` and returns false on failure, but
  `state_run.c` ignores the return value. That reaches the database as exactly
  **−2048.00 hPa and −327.68 °C**. The checker reports these separately from
  ordinary out-of-range values.
- **A stuck bus reads one value forever.** Real pressure moves by more than
  the 0.0625 hPa LSB over any useful window, so a run with one distinct
  pressure value did not measure pressure.
- **A wrong conversion** stays a valid float while landing outside any
  possible air pressure.

| Stream | Bound | Rationale |
| --- | --- | --- |
| Pressure | 900–1100 hPa (defaults) | bench ambient; the LPS27 is specified over 260–1260 hPa |
| Temperature | 15–40 °C | room temperature |
| Voltage | 2.0–3.7 V | below 2.00 V the firmware's `LOGWRITE_BAT` would have tripped |
| Distinct pressure values | > 1 | see above |

Override the bounds with `--pressure-min/--pressure-max` and
`--temp-min/--temp-max`. Compare against a local **station** pressure, not the
sea-level-adjusted figure, which reads ~30 hPa high at this elevation.

The block-level `temperature` field in `PresTagLog` is **not a temperature**.
`state_run.c` stores a mid-block Vdd sample there. The SQLite schema says so,
and the `Temperature` table comes from the per-sample values, which are real.

`power_experiment.check_download()` takes its expected rate from `lsm6.odr`,
so its rate check does not run for a PresTag. Use the checker above.

## 5. Gates

- **`IDLE` and `FINISHED` (A1, A3, A5) below 1 µA.** Hard.
- A1 and A5 within 20% of each other.
- The Shutdown fit's `I_rest` is below 1 µA and agrees with A1.
- `I_avg` at 60 s is below 1 µA.
- `I_avg` at 90 s meets the one-year budget: ≤ 1.256 µA on 11 mAh. On 5.5 mAh
  (0.628 µA), record whether it clears.
- The fit's residuals are under 5%.
- Every Phase C expectation and every Phase D check passes, values as well as
  structure.

Against the baseline in [`power-results.md`](power-results.md), a resting state
or sweep point that moves by more than 20%, or A1 and A5 diverging, is a
finding to investigate before shipping. At 90 s, 20% is about 0.1 µA. On a
5.5 mAh cell that is the difference between making a year and missing it.

## 6. Recording results

Append each session to [`power-results.md`](power-results.md). Record what the
shared
[recording checklist](../../../../../docs/bench/power-testing.md#7-recording-a-session)
asks for, plus the firmware string `tag-info` reports.
