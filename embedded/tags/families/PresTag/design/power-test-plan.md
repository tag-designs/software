# PresTag Power and Schedule Test Plan

Hardware-in-the-loop plan for a `PresTag` target on a PresTagv3 board, wired to
a Joulescope through a baseboard. It covers the four things that decide whether
a deployed PresTag behaves: **sample period**, **scheduled start**, **stop**,
and **hibernation**.

The plan separates two questions that are easy to conflate:

- *How much current does the tag draw in each state?* — answered by the period
  sweep and the resting-state measurements, which need whole repeating units of
  the sampling cycle to be unbiased.
- *Does the schedule do what it was told?* — answered by the transition tests,
  which need only one representative period.

Per-parameter cross-products are deliberately **not** run. Start, stop and
hibernation are period-independent control paths; they are exercised once, at a
single field-representative period.

## 1. What the firmware actually does

Read this before running anything. Several of the plan's choices only make sense
against these behaviours, and three of them contradict the comments in the code.

### 1.1 Sample period

`Config.period` (protobuf) becomes `sconfig.lps_period`, in **seconds**.
`Running(T_INIT)` arms the RTC periodic wakeup with
`enableTicker(sconfig.lps_period)`
([state_run.c:59](../src/state_run.c#L59)). `enableTicker()` returns without
arming anything for `secs < 1`, and the wakeup register is 16-bit, so the usable
range is **1 s to 65536 s**.

`writeConfig()` **rejects a configuration whose `period` is zero**
([config.c:79](../src/config.c#L79)) and only accepts one at all while the tag
is `IDLE`.

### 1.2 The sleep mode changes at 10 seconds

The last statement of `Running()` picks the between-sample sleep mode
([state_run.c:170](../src/state_run.c#L170)):

```c
if (sconfig.lps_period < 10)
  return STOP2;
else
  return PRESTAG_RUNNING_LONG_SLEEP_MODE;   /* SHUTDOWN on PresTag */
```

This is the single most important fact in the plan. Below 10 s the MCU sleeps in
**Stop 2** and retains SRAM. At 10 s and above it enters **Shutdown** and
therefore **reboots for every sample**: SRAM is lost, `crt0` runs, the clock
tree and devices re-initialise, and the run state is recovered from the RTC
backup registers via `pState`.

Per-sample energy is consequently discontinuous at the boundary, and average
current need not be monotonic in period across it. Both sides are measured, but
only the Shutdown side is field-relevant — **periods below 10 s are a bench
configuration** and are characterised for power only, not for schedule
behaviour.

`PresTag/inc/custom.h` sets every other state to Shutdown as well:
`TAG_IDLE_SLEEP_MODE`, `TAG_CONFIGURED_SLEEP_MODE`, `TAG_HIBERNATING_SLEEP_MODE`,
`TAG_FINISHED_SLEEP_MODE` and `TAG_ABORTED_SLEEP_MODE`.

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
([pwr.c:234](../../../common/core/src/pwr.c#L234)):

```c
void godown(enum Sleep sleepmode)
{
  tagPowerEnterTerminalSleep(sleepmode);
}
```

`tagPowerEnterTerminalSleep()` on this part selects `LPMS` only for Standby and
Shutdown, and returns without touching anything for any other request
([pwr-l432.c:33-45](../../../common/core/src/pwr-l432.c#L33-L45)). **A `STOP2`
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

### 1.2b Measured: `stopMilliseconds()` spins ~6.7 ms and never sleeps

Every driver wait on this target goes through `stopMilliseconds()`, which arms
LPTIM1 for the requested delay and enters Stop 2 with `WFE`. Instrumented with
retained timestamps (see §11), the call behaves like this for requests of 2 ms
and 5 ms alike:

| Step | Measured |
| --- | --- |
| bus disable, LPTIM clock/enable | ~0.1 ms |
| **`ARR` write → `ARROK` seen** (`while (!(ISR & ARROK)) {}`) | **6.3–7.1 ms, Run current** |
| `WFE` loop until `ARRM` | **0.0–0.1 ms, exactly 1 iteration** |

The cause is the clock. **LSE on this board is 1024 Hz**, and LPTIM1 runs from
it (`RCC_CCIPR.LPTIM1SEL = 11`, read live), so `TAG_STOP_LPTIM_HZ = 1024` is
right and a 5 ms request correctly programs 6 ticks. But LPTIM register writes
— `ARR`, and every `ICR` flag clear — only take effect after kernel-clock edges,
and at 1024 Hz each edge is ~1 ms. The `ARROK` wait therefore burns ~7 cycles of
Run current, longer than the delay it is arming; and the `ARRM` clear written
immediately before the `WFE` has not propagated when the loop reads `ISR`, so
it sees the *previous* call's match still set and returns without sleeping.
Net effect: **the Stop 2 sleep never happens, the wait lasts ~6.7 ms whatever
was requested, and all of it is at Run current** — three per sample (two LPS27
waits, one flash-wake wait), ~20 ms and roughly a third of `Q_cycle`, the flash
one dearest because the AT25 is awake underneath it.

The AT25 recharge metering is therefore over-satisfied in time (7 ms for a 2 ms
request) but paid for at Run current instead of in Stop.

Fix direction: the synchronisation cost has to become small relative to the
delay, and the flag check must not race the clear. Options, in rough order of
preference: (a) clock LPTIM1 from LSI (~32 kHz, available in Stop 2; `LPTIM1SEL`
is independent of `RTCSEL`, so the RTC stays on the 1024 Hz LSE) with a
prescaler to taste and `TAG_STOP_LPTIM_HZ` updated — sync drops to tens of µs;
(b) keep 1024 Hz but never rewrite `ARR`: free-run, use `CMP = CNT + ticks` and
wait for `CMPOK` *by sleeping*, accepting ~1–2 ms of granularity; (c) the RTC
wakeup timer, which has the same LSE-domain sync cost. Whichever is chosen must
then be shown to sleep — a dip to ~2 µA for the requested duration in a fine
trace — rather than argued.

### 1.3 The repeating unit is 60 samples, not one sample

Two different flash writes happen at two different cadences
([state_run.c:104-146](../src/state_run.c#L104-L146)):

| Event | Cadence | Cost |
| --- | --- | --- |
| External flash sample write (`writeDataLog`) | **every sample** | AT25 wake, 4-byte program, sleep |
| Internal flash header (`writeDataHeader`) | **every 60 samples** (`DATALOG_SAMPLES`) | internal flash unlock/program/lock, plus `stopMilliseconds(2)` |

So the energy cycle repeats every `60 × period` seconds, not every period. **A
measurement window that is not a whole number of 60-sample blocks is biased** —
it either includes or excludes a header write that the average should have
amortised over 60 samples. This is what "measure for some multiple of the sample
period" has to mean here: a multiple of *sixty* sample periods.

A fresh run after `tag-reset` starts at `external_blocks == 0`, so header writes
land at samples 0, 60, 120, … and their positions in time are predictable from
the moment the tag enters `RUNNING`, with no need to attach a monitor.

### 1.4 Start: `start_delay` is ignored, and start is polled once a minute

`PresTag::writeConfig()` takes the start time straight from the schedule
([config.c:75](../src/config.c#L75)):

```c
config_tmp.start = config->active_interval.start_epoch;
config_tmp.stop  = config->active_interval.end_epoch;
```

It never reads `config->start_delay`. That field is an **IMUTag** mechanism
([families/IMUTag/src/config.c:261-264](../../IMUTag/src/config.c#L261-L264)),
where it is converted into an absolute start. Two consequences:

- **`tag-start --start-now` is a no-op on a PresTag.** It sets `start_delay = 0`,
  which this family discards. To start immediately, set
  `active_interval.start_epoch` to zero or to a past epoch.
- Scheduled start is expressed *only* as an absolute `start_epoch`.

`PresTag` also does **not** define `TAG_CONFIGURED_IMMEDIATE_START`, which
defaults to 0 ([state_machine.c:73](../../../common/core/src/state_machine.c#L73)).
So a start command always lands in `CONFIGURED`, which arms `enableAlarm(1,
ALARM_MINUTE)` and sleeps in Shutdown, waking once a minute to test
`timestamp >= sconfig.start`. **Even an already-due start takes up to ~60 s to
reach `RUNNING`**, and `CONFIGURED` is itself a genuine low-power waiting state —
the state a configured, not-yet-started tag sits in before deployment.

### 1.5 Stop

Two independent paths reach a terminal state:

- **Scheduled**: `Running()` checks `sconfig.stop < timestamp` on each wakeup
  ([state_run.c:68](../src/state_run.c#L68)) and goes to `Finished`. Latency is
  therefore up to one sample period.
- **Commanded**: `tag-stop` posts a work bit; the state machine acts later.

`Tag::Stop()` returning true means the request was *accepted*, not performed.
Poll for the state, as AGENTS.md requires; `tag-stop` has `--settle-timeout` for
exactly this.

### 1.6 Hibernation, and two things about it that surprise

Entry, from `Running()` ([state_run.c:78-85](../src/state_run.c#L78-L85)):

```c
if ((timestamp >= sconfig.hibernate[i].start_epoch) &&
    (timestamp <  sconfig.hibernate[i].end_epoch) &&
    (pState->external_blocks % (sizeof(t_DataLog) / 2) == 0))
  return Hibernating(T_INIT, State_EVENT_STARTHIB);
```

`DATALOG_SAMPLES` is 60, so the gate is `external_blocks % 60 == 0`: one entry
opportunity per header block, or every `60 × period` seconds. At a 10 s period
that is every 10 minutes; at the shipped default of 90 s, every 90
minutes. **A
hibernation window shorter than `60 × period` still contains no entry
opportunity and is skipped**, which is worth knowing when choosing windows at
long periods.

This gate read `sizeof(t_DataLog)/2` — 120, twice the page — until the fix that
accompanies this plan. `external_blocks` counts 4-byte samples, so 120 was the
page measured in 16-bit words, and entry was possible only every *other* block.
The same constant appeared in the `T_INIT` cursor round-up, where it was not
merely coarse; see §1.7.

Exit, from `Hibernating(T_INIT)`
([state_machine.c:1189-1193](../../../common/core/src/state_machine.c#L1189-L1193)):

```c
// set 1 hour wakeup interval
disableAllAlarms();
disableTicker();
enableAlarm(1, ALARM_HOUR);
```

The comment says one hour. It is **once a minute**. In `enableAlarm()`,
`ALARM_HOUR` and `ALARM_MINUTE` set an identical mask
([time.c:415-420](../../../common/core/src/time.c#L415-L420)):

```c
case ALARM_MINUTE:
  alarmspec.alrmr = (RTC_ALRMAR_MSK4 | RTC_ALRMAR_MSK3 | RTC_ALRMAR_MSK2);
  break;
case ALARM_HOUR:
  alarmspec.alrmr = (RTC_ALRMAR_MSK4 | RTC_ALRMAR_MSK3 | RTC_ALRMAR_MSK2);
  break;
```

`MSK2` masks the minutes field, leaving only seconds unmasked, so the alarm
matches whenever seconds == 0 — once per minute. An hourly alarm would need
`MSK2` clear so that both minutes and seconds must match. Hibernation therefore
costs 60 wakeups an hour rather than one, and hibernation exit is prompt
(≤ ~60 s) rather than up to an hour late.

Test **H3** measures this directly rather than arguing about it. These are
observations, not changes: this plan does not modify firmware.

### 1.7 The log invariant, and the round-up that used to break it

`Running(T_INIT)` rounds the sample cursor up to a whole page
([state_run.c:58-60](../src/state_run.c#L58-L60)):

```c
int remainder = pState->external_blocks % DATALOG_SAMPLES;
if (remainder)
  pState->external_blocks = pState->external_blocks + DATALOG_SAMPLES - remainder;
```

The invariant it must preserve is `external_blocks == pages × DATALOG_SAMPLES`,
because the download pairs positionally: block `index` reads its header from
`vddHeader[index]` and its samples from byte offset `index × sizeof(t_DataLog)`
([datalog.c:367-380](../src/datalog.c#L367-L380)). `restoreLog()` establishes
exactly that, `external_blocks = pages × DATALOG_SAMPLES`
([datalog.c:235-239](../src/datalog.c#L235-L239)).

**This read `sizeof(t_DataLog)/2` — 120, twice the page — until the fix that
accompanies this plan.** `external_blocks` counts 4-byte samples (`writeDataLog`
uses `external_blocks * 4`), so 120 was the page measured in 16-bit words. At
every reachable `Running(T_INIT)` the cursor is already a multiple of 60, so the
round-up could only ever break the invariant, never repair it:

- from `Configured`, the cursor is 0 — no-op;
- from a normal hibernation exit, the entry gate had left it 120-aligned — no-op;
- from `restoreLog()` after a **brownout**, the cursor is `60 × pages`. With
  `pages` **odd**, `60 × pages mod 120 == 60`, so it advanced a whole page while
  `pages` stayed put.

After that the next header was written at `vddHeader[pages]` but its samples
landed in page `pages + 1`, and the pairing stayed off by one page **for the
rest of the run**: one block downloaded with a header and zero samples (its page
erased, the reader stopping at the first `pressure == -1`), and every later block
was served samples taken `60 × period` seconds before its header claimed. At the
shipped default of 90 s that is 90 minutes of skew on everything after the
recovery.

The two live routes were `Running(T_INIT, State_EVENT_BROWNOUT)`
([state_machine.c:790](../../../common/core/src/state_machine.c#L790)) and a
hibernation exit following a brownout, where `restoreLog()` had already replaced
the 120-aligned cursor with `60 × pages`.

Restart recovery is otherwise **benign on a PresTag**, and that is what made this
the whole of the risk. Unlike IMUTag there is no sensor state to rebuild: the
LPS27 is read one-shot per sample, with no FIFO phase, watermark, or streaming
ownership to resynchronise, and the pressure sample taken after a reset is as
good as the one before it. `State_EVENT_BROWNOUT` therefore has nothing to repair
on this family except the log cursor — so the cursor arithmetic in
`Running(T_INIT)` was not one hazard among several, it was the only thing restart
recovery had to get right.

**Both constants were changed together, and had to be.** They were consistent
with each other, which is why normal hibernation exit was unaffected. Correcting
the entry gate alone would have made every odd-page hibernation exit
desynchronise the log — turning a granularity wart into the corruption above.

**T4** below is the regression test.

## 2. Rig discipline

Non-negotiable, and each item has already cost real time on this project.

- **Detach the Joulescope desktop app and `qtmonitor` before every measurement,
  and confirm it.** The desktop app holds the device so the script cannot open
  it. `qtmonitor` holds the monitor, which keeps `isMonitorEnabled()` true — and
  `tagPowerEnterTerminalSleep()` **returns immediately without sleeping** when
  the monitor is enabled ([pwr-l432.c:29-32](../../../common/core/src/pwr-l432.c#L29-L32)).
  A held monitor reads as a plausible-looking high average, not as an error.
- **Use the Joulescope server for the whole session.** Opening and closing the
  instrument per measurement wedges it and can leave the DUT supply off, which
  then surfaces as `Unable to get core ID` from the debug probe.
- **Do not use the `joulescope-js220` MCP server**; it holds the device for the
  session and blocks the harness.
- **Take the `charge/time` figure, not the window mean.** These loads are
  strongly duty-cycled; only the charge integral is meaningful.
- **Two or more windows per point, and they must agree.** A single window hides
  both a still-attached monitor and a tag waking on a period you did not expect.

```sh
<python-with-pyjoulescope> embedded/tools/joulescope_server.py --start &
# ... all measurements via --use-server ...
embedded/tools/joulescope_server.py --stop
```

### 2.0 Flashing: stop and reset the tag first

**Always `tag-stop` then `tag-reset` before programming.** Not as hygiene — as
the thing that makes the download work at all.

Reset drops the tag into recovery. A freshly programmed image boots, reads the
state markers in internal flash together with `pState` in the RTC backup
registers, resumes whatever the tag was previously doing, and re-enters Shutdown
within milliseconds. Neither the flash markers nor the backup domain is cleared
by programming, so the persistent state outlives the image that wrote it. The
next connection attempt then meets a sleeping part with its debug port powered
down, and fails with `Unable to get core ID` at a perfectly healthy target
voltage — the same message the probe gives for an unpowered target, which is why
it reads as a rig fault.

Clearing the state first removes the cause:

```sh
build-host/bin/tag-stop     # RUNNING -> FINISHED; poll, an ack is not a completion
build-host/bin/tag-reset    # erase -> IDLE
cmake --build <build-dir> --target PresTag-download
```

Two supporting changes are in the tree, and neither replaces the procedure
above:

- The download target still ends with **`-g 0x08000000`**, a debugger jump, and
  must keep doing so. During the first execution of this plan it was changed to
  `-rst` (a system reset) on the theory that a jump left debug state behind;
  that was wrong, and expensively so — see §2.0a. The jump never consults boot
  selection; a reset does.
- `stm32_programmer_select.py` retries the connection four times, two seconds
  apart, and connects with `reset=HWrst`. A first attempt on a sleeping target
  still fails intermittently.

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

### 2.1 Measuring a sub-µA average of a load that pulses to milliamps

This is the hardest part of the plan, and it is easy to get a plausible wrong
number. In `RUNNING` the tag sits at a few hundred nA and jumps to milliamps for
~10 ms per sample: at the 90 s default that is a duty cycle around 1 in 10,000,
and four decades of dynamic range inside one measurement.

- **Leave the Joulescope auto-ranging.** Manual ranging cannot work here: a range
  that resolves a 300 nA floor clips a 3 mA wake, and a range that captures the
  wake cannot see the floor. `joulescope_measure.py` leaves the device as found
  unless `--set-range` is given, so confirm it is on auto rather than assuming.
- **Take `charge/time`, never the window mean.** With a 1-in-10,000 duty cycle
  the mean of per-window statistics is dominated by whichever windows happened
  to contain a pulse. Only the charge integral is meaningful, which is why the
  block-multiple window rule in §5 exists.
- **Require enough wake events per window.** One 60-sample block gives 60 of
  them at any period, which is the real reason the window rule is stated in
  blocks rather than seconds.
- **The resting measurements are the delicate ones.** `IDLE` and `FINISHED` have
  no pulses at all, so they are pure DC readings a couple of decades above the
  instrument's own offset and noise. Two windows agreeing to a few percent is
  the evidence that the number is real. Treat a surprisingly *low* reading —
  tens of nA — with the same suspicion as a high one; it more likely means the
  sense path is not carrying the tag's current than that the tag is
  extraordinary.
- **Cross-check the floor two ways.** The fit intercept `I_rest` from §5.2 and
  the directly measured `IDLE` current in §4 are independent routes to the same
  quantity. They should agree. If they do not, at least one of the sweep points
  is wrong, and the fit will usually be the one to distrust because a single bad
  point moves the intercept a long way.

### 2.1 Note on inherited power lore

PresTagv3 is an **STM32L432**
([board-customizations.json](../../../../boards/PresTagv3/cfg/board-customizations.json)),
so it uses `pwr-l432.c` and has a real, working Shutdown mode. The Standby /
Stop 3 findings in AGENTS.md are about the **STM32U375** and do not transfer.
What does transfer is the discipline: on these parts a change that provably
cannot alter behaviour has moved idle current by orders of magnitude, so
**measure after every firmware change** and suspect layout before logic.

## 3. Configuration templates

`tag-start -c FILE` without `--merge` programs exactly what the file says, which
is what a reproducible experiment wants. Field names are protobuf-JSON, matching
[`host/docs/fixtures/qtmonitor/prestag.json`](../../../../../host/docs/fixtures/qtmonitor/prestag.json).

Base template, with a period to substitute and hibernation disabled:

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

`start_epoch: 0` starts as soon as `CONFIGURED` next polls. Absolute epochs for
the scheduled tests are generated at run time:

```sh
now=$(date +%s)
start=$((now + 300))     # start five minutes out
```

[`embedded/tools/prestag_mkconfig.py`](../../../../tools/prestag_mkconfig.py)
turns the plan's "start five minutes out, hibernate from +25 to +45 minutes"
offsets into absolute epochs, and prints the epochs it chose so the run can be
checked against what was actually programmed rather than what was intended:

```sh
embedded/tools/prestag_mkconfig.py --period 10 --out /tmp/p10.json
embedded/tools/prestag_mkconfig.py --period 10 --run-for 5400 \
    --hibernate 1500:2700 --out /tmp/hib.json
```

It refuses a zero period and a third hibernation window, both of which are
silently discarded further down (§1.1, and `hibernate[2]` in `t_storedconfig`).
The JSON above can equally be edited by hand.

Three things about the shipped defaults are worth knowing before treating any of
them as a reference:

- **`PresTag` defaults to `period: 90`**, in
  [`embedded/proto-c/prestag-proto-c/default-config.json`](../../../../proto-c/prestag-proto-c/default-config.json).
  That is the deployment period this plan measures at B6 and C6.
- **`PresTagRaw` defaults to `period: 60`**, so the two variants of this family
  do not ship the same schedule. Check which target is on the bench before
  quoting a "default".
- The `PresTag` default file has a **trailing comma** after `"period": 90`, so it
  is not valid strict JSON and `json.load` rejects it. Do not assume
  `tag-start -c` can consume it unmodified; the templates above are written out
  in full for that reason. Fixing that file is out of scope here.

## 4. Phase A — resting states

Every PresTag rest state is Shutdown, but they are **not** all equally quiet:
`CONFIGURED` and `HIBERNATING` wake once a minute (§1.4, §1.6), while `IDLE` and
`FINISHED` should be flat.

Because of that minute alarm, **windows must be a whole number of minutes and at
least five**, so the per-minute wake is amortised the same way in every window.
A 45 s window can straddle zero or one wake and produce two different answers
from a healthy tag.

| ID | State | How to reach it | Window | Repeats |
| --- | --- | --- | --- | --- |
| A1 | `IDLE`, clock set | `tag-reset --set-rtc` | 300 s | 3 |
| A2 | `CONFIGURED` | start with `start_epoch = now + 3600` | 900 s | 2 |
| A3 | `RUNNING`→`FINISHED` | run briefly, then `tag-stop` | 300 s | 3 |
| A4 | `HIBERNATING` | see H2 | 900 s | 2 |
| A5 | `IDLE` again | `tag-reset --set-rtc` after download | 300 s | 3 |

A2 and A4 get 900 s rather than 300 s because they are pulsed, not flat: at one
wake per minute a 300 s window holds only five events, so the average is set by
how many happened to land inside it. Fifteen is enough to be worth quoting. A1,
A3 and A5 are flat and need only enough time to average the instrument's noise.

A1 sets the clock deliberately: idle-with-the-clock-set is the state a prepared
tag actually sits in for weeks, and it is the state in which a whole class of
regression hides. A1 and A5 are the same logical state reached by two different
histories; **comparing them is the point**, and a divergence between them is a
finding even when both numbers look plausible.

**Gate: the quiescent states must be below 1 µA.** `IDLE` and `FINISHED` are
genuinely quiescent — Shutdown with nothing pending — and the floor is the
STM32L432 in Shutdown plus the RV3028, the LPS27 powered down and the AT25 in
deep power-down. At or above 1 µA there, something is still powered or a pin is
being driven against a pull-up, and that is a failure however plausible the
number looks.

In practice expect the floor to be **a few hundred nA**, well under the 1 µA
gate: the tag has to average under 1 µA at a 60 s period *including* sampling
(§5.2), which leaves only a fraction of a µA for the floor. Record the actual
number — it is what the whole lifetime estimate rests on.

`CONFIGURED` and `HIBERNATING` are **not** quiescent: each carries one wake per
minute (§1.4, §1.6), so their average sits above the floor by the wake charge
divided by 60 s. At tens of µC per wake that is on the order of a µA, so these
two states may well cost *more than sampling does* at a 90 s period — which is
worth knowing before leaving a tag in `CONFIGURED` for weeks awaiting
deployment. Record them, but do not hold them to 1 µA. The number to
extract is how far above `IDLE` they sit — that difference *is* the cost of the
minute alarm, and it is what **H3** quantifies.

Do not borrow `tag_lifecycle_check.py`'s `DEFAULT_IDLE_MAX_UA` of 100 µA here.
That is an IMUTag "did it sleep at all" threshold on a different part; a PresTag
sitting at 50 µA in `IDLE` would sail through it while being two orders of
magnitude out.

## 5. Phase B — sample period sweep

Purpose: characterise average current against period, and separate the
fixed resting cost from the per-sample cost.

**Window rule:** the window's *duration* must be an integer multiple of
`60 × period`. Its **starting phase does not matter** — a window exactly
`k × 60 × period` seconds long contains exactly `k` header writes wherever it
begins, so there is no need to align it to a block boundary. Only wait long
enough after the tag reaches `RUNNING` to clear the start transient: three
sample periods, or 60 s, whichever is longer.

That is worth stating explicitly because the obvious stronger rule — "start on a
block boundary" — doubles the cost of every long point for no benefit, and at
90 s a block is already 90 minutes.

| ID | Period | Regime | Window (1 block) | Repeats | Field-relevant |
| --- | --- | --- | --- | --- | --- |
| B1 | 1 s | Stop 2 | 60 s | 3 | bench only |
| B2 | 9 s | Stop 2 | 540 s | 2 | bench only; boundary, low side |
| B3 | 10 s | **Shutdown** | 600 s | 2 | boundary, high side |
| B4 | 15 s | Shutdown | 900 s | 2 | yes |
| B5 | 30 s | Shutdown | 1800 s | 1 | yes |
| B6 | 90 s | Shutdown | 5400 s | 1 | yes — **the shipped default** |
| B7 | 60 s | Shutdown | 3600 s | 1 | optional, extra fit leverage |

Sub-10 s is not an operating regime — it is a bench configuration — so it gets
**two points, not four**. Two determine the Stop 2 line exactly, with no
redundancy and therefore no residual check, which is the right amount of effort
for a regime nothing is deployed in. 1 s and 9 s are chosen for maximum spread
in `1/T`.

B2/B3 are the pair that matters: **9 s and 10 s differ only in sleep mode**, so
the difference between them is the cost of rebooting per sample, measured
directly rather than inferred.

The four field points B3–B6 span `1/T` from 0.100 to 0.011, which is ample
leverage for the fit in §5.2 and leaves a real residual check. Total measurement
time is about 2.9 hours.

Each point is a full cycle — the config can only be written in `IDLE`, and a
fresh erase keeps block boundaries predictable:

```sh
build-host/bin/tag-reset --set-rtc          # stop, erase, IDLE, clock set
build-host/bin/tag-start -c /tmp/p10.json   # -> CONFIGURED -> RUNNING (≤60 s)
# confirm RUNNING, note the time, wait one block, then:
embedded/tools/joulescope_measure.py --use-server --duration 600 --window 0.5 --repeat 2
```

Do **not** pass `--start-now`; it does nothing here (§1.4) and stating otherwise
in a log would be misleading.

### 5.1 Model to fit

For a duty-cycled sampler the average current is

```
I_avg(T) = I_rest + (Q_sample + Q_header/60) / T
```

where `T` is the period, `I_rest` the resting current between samples,
`Q_sample` the charge for one wake-read-write, and `Q_header` the extra charge
for an internal-flash header. Plotting `I_avg` against `1/T` gives a straight
line: **intercept is `I_rest`, slope is `Q_sample + Q_header/60`**.

Fit **two separate lines** — B1–B2 (Stop 2, two points, exact) and B3–B6
(Shutdown, four points). They are different mechanisms and a single fit across
the boundary is meaningless. The
Shutdown slope includes the per-sample reboot; the difference between the two
slopes is what a field deployment pays for Shutdown sampling.

`Q_header` can be isolated, if wanted, by comparing a window containing a header
write with an adjacent window that does not, at a period where a block is short
enough to place windows precisely (B3, or B1 where a block is only 60 s).

### 5.2 Average current above 10 s, and battery lifetime

Periods at or above 10 s are the field regime, and it is the simple one: the tag
is in Shutdown between samples, so the between-sample current does not depend on
the period, and every sample costs the same fixed packet of charge — wake,
reboot, sensor read, external-flash write — regardless of how far apart the
samples are. That gives a **strictly linear law in sample rate**:

```
I_avg(T)  =  I_rest  +  Q_cycle / T                    [µA, with Q in µC, T in s]

            where  Q_cycle = Q_sample + Q_header / 60
```

`I_rest` is the Shutdown floor: MCU Shutdown leakage, the RV3028 RTC, the LPS27
in power-down and the AT25 in deep power-down, plus board leakage. `Q_sample` is
the charge for one wake-to-sleep sampling cycle. `Q_header` is the extra charge
for the internal-flash header written once per 60 samples (§1.3), which is why
it appears divided by 60 — it is a real cost, but an amortised one.

Two properties make this useful:

- **It is linear in `1/T`**, so two measured periods determine it and four
  overdetermine it. Fit B3–B6 by least squares on `(1/T, I_avg)`: the intercept
  is `I_rest` and the slope is `Q_cycle`.
- **It saturates.** As `T` grows, `I_avg → I_rest`. There is a maximum lifetime
  no choice of period can beat, and a knee period

  ```
  T_knee = Q_cycle / I_rest
  ```

  at which sampling costs exactly as much as resting. Below the knee, doubling
  the period nearly halves the current; well above it, doubling the period buys
  almost nothing. **`T_knee` is the single most useful number this plan
  produces** for choosing a deployment period.

  With a Shutdown floor below 1 µA (§4), expect `T_knee` to be **long** — on the
  order of `Q_cycle / 1 µA`, which for a few hundred µC per sample is several
  hundred seconds or more. The practical consequence is that across the whole
  usable period range the tag is **sampling-dominated**, and lifetime is close
  to proportional to the period: doubling 90 s to 180 s should come near to
  doubling the deployment. That is a strong prediction, and if the measured
  sweep does not show it, either the floor is not sub-µA or `Q_cycle` is not
  constant — both worth chasing before quoting a lifetime.

Note this law does *not* extend below 10 s. There the tag sleeps in Stop 2, so
`I_rest` is the much higher Stop 2 retention current and `Q_sample` excludes the
reboot; it is a different line with a different intercept and slope (§5.1).

#### The target, and what it implies

**A healthy PresTag averages under 1 µA at a 60 s period**, and the deployment
goal is a lifetime approaching **one year**. Those two numbers fix the scale of
everything measured here, so state them before taking any reading:

| Cell | Average current for 365 days |
| --- | --- |
| 5.5 mAh | **0.628 µA** |
| 11 mAh | **1.256 µA** |

`1000 × C / (365 × 24)`. So an 11 mAh cell reaches a year at 1.26 µA, which the
sub-1 µA figure at 60 s already clears; a 5.5 mAh cell needs 0.628 µA and is
**marginal**, its outcome decided by the resting floor and by derating rather
than by the sample period.

Working backwards through `I_avg = I_rest + Q_cycle / T` at the 90 s default: if
the floor is ~0.3 µA, a 5.5 mAh cell allows `Q_cycle ≲ 30 µC` and an 11 mAh cell
`Q_cycle ≲ 86 µC`. **`Q_cycle` for this tag is therefore tens of microcoulombs,
not hundreds** — about 10 ms at a few mA for the whole wake, reboot, LPS27
one-shot and flash write.

That is the sanity check to carry into every measurement. The whole field range
is roughly **0.5 µA to 4 µA**. A sweep point in the tens of µA, or a fit
returning hundreds of µC, is a broken measurement — a monitor still attached, or
a pin driven against a pull-up — not a slow tag. `prestag_power_model.py`
warns on both, and `--target-days 365` marks each period as meeting or missing
the goal.

#### Lifetime

For a cell of capacity `C` in mAh, ignoring self-discharge and voltage cutoff:

```
lifetime (days)  =  1000 × C  /  ( 24 × I_avg )        [C in mAh, I_avg in µA]
```

The two cells of interest give a convenient shorthand:

| Capacity | days ≈ | at 0.5 µA | at 1 µA | at 2 µA | at 4 µA |
| --- | --- | --- | --- | --- | --- |
| 5.5 mAh | 229 / I_avg[µA] | 458 d | 229 d | 115 d | 57 d |
| 11 mAh | 458 / I_avg[µA] | 917 d | 458 d | 229 d | 115 d |

Those columns are the *arithmetic*, not a prediction — the predicted lifetime
comes from substituting the fitted `I_avg(T)`. Fill in §9's lifetime table from
the fit once B3–B6 are measured.

**Derate before believing a deployment number.** The formula is an upper bound:

- **Self-discharge may set the lifetime rather than the load.** At ~1 µA the tag
  draws about 8.8 mAh a year, which is the same order as the cells themselves,
  so a cell losing a few percent a year is spending a real fraction of what is
  left. Over a year-long deployment the self-discharge term is not a correction
  to the answer, it is part of the answer; use the manufacturer's figure for the
  actual cell rather than a rule of thumb.
- **Usable capacity is below nominal**, because the tag stops working at a
  cutoff voltage well above full discharge, and the rated capacity assumes a
  discharge rate and temperature the deployment may not match.
- **Cold reduces both** capacity and effective voltage, and raises the LPS27 and
  flash write times slightly.
- **The current peaks matter for the cell, not just the average.** A per-sample
  flash write draws milliamps for a short time; a small cell with high internal
  impedance will sag under that, and the sag — not the average — is what
  determines whether the tag browns out near end of life.

Record the fit and the derated estimate separately in the report so the
assumption is visible rather than buried in a single number.

#### Producing the numbers

`embedded/tools/prestag_power_model.py` does the fit and the lifetime tables
from the measured sweep, so the arithmetic is not redone by hand:

```sh
embedded/tools/prestag_power_model.py \
    --point 10:<uA> --point 15:<uA> --point 30:<uA> --point 90:<uA> \
    --capacity 5.5 --capacity 11 --target-days 365
```

It prints `I_rest`, `Q_cycle`, `T_knee`, the fit residuals, and a lifetime table
per capacity. It also reports the residuals prominently: a poor straight-line
fit means one of the measurements is wrong or the regime assumption has broken,
and that is a finding, not a rounding detail.

## 6. Phase C — schedule behaviour

All of Phase C runs at a **single period, `T = 10 s`**, chosen as the shortest
field-representative period: it is on the Shutdown side of the boundary, gives a
header block every 10 minutes, and a hibernation entry opportunity every 20
minutes (§1.6). Nothing here is period-dependent, so it is not repeated at other
periods. One optional confirmation at `T = 60 s` is listed at the end.

These tests are primarily **functional** — the question is whether the schedule
is honoured and the data is right. Power is recorded where the state is a
resting one.

### C1 — Scheduled start

1. `tag-reset --set-rtc`.
2. Program `start_epoch = now + 300`, `end_epoch = 2147483647`, period 10.
3. `tag-start -c <file>`; confirm the tag reports `CONFIGURED`, not `RUNNING`.
4. Detach, measure 300 s (this is **A2**).
5. Poll `tag-info` from just before the start epoch until `RUNNING`.

**Expected:** stays `CONFIGURED` until the start epoch; reaches `RUNNING` within
one minute after it (minute-alarm polling, §1.4). Records the observed latency.

**Watch for:** an immediate transition to `RUNNING`, which would mean
`start_epoch` was not programmed as intended — the most likely cause being a
`--merge` that left the tag's old schedule in place.

### C2 — Immediate start

Same, with `start_epoch = 0`. **Expected:** `CONFIGURED` briefly, then `RUNNING`
within ~60 s. This is the baseline latency that C1 is measured against, and it
is the test that demonstrates `--start-now` is not what causes an immediate
start.

### C3 — Scheduled stop

1. Fresh run at period 10 with `end_epoch = now + 900`.
2. Leave detached; poll `tag-info` from just before the stop epoch.

**Expected:** `FINISHED` within one sample period (10 s) of the stop epoch
(§1.5). Then measure the resting `FINISHED` state (**A3**).

### C4 — Commanded stop

1. Fresh run at period 10, open-ended.
2. `build-host/bin/tag-stop`, then poll for `FINISHED`.

**Expected:** `tag-stop` exits 0 **and** the tag is observed in `FINISHED` by
polling. An acknowledgement is not a completion; a tool that reads status
immediately can still see `RUNNING`. Record the number of polls needed.

**Then attempt a download immediately.** A download refused with "Can't dump logs
from current state" straight after a successful-looking stop is the exact
signature of the acknowledgement-versus-completion trap, and confirming it does
*not* happen is part of the test.

### C5 — Hibernation entry and exit

Choose the window so entry is taken mid-run rather than immediately. At
`T = 10 s` a block is 600 s, so entry opportunities fall every 10 minutes from
the start of the run.

1. `tag-reset --set-rtc`, so the run begins at `external_blocks == 0`.
2. Program period 10, `start_epoch = 0`, `end_epoch = now + 5400`, and
   `hibernate[0] = { now + 1500, now + 2700 }` — a 20-minute window opening
   ~25 minutes into the run, so entry is taken at the block boundary at
   t ≈ 1800 s rather than at t = 0.
3. Start, detach, and poll `tag-info` infrequently — each attach resets the tag,
   so poll only around the expected transitions, and record every attach.

**Expected:**
- Stays `RUNNING` until the first 60-sample block boundary at or after the
  window opens — t ≈ 1800 s — and enters `HIBERNATING` there, **not** at the
  instant the window opens.
- Before the fix in §1.7 this would have been the 120-sample boundary. The
  window is 20 minutes wide, which spans two block boundaries at this period,
  so it would have been entered either way; the test discriminates on *which*
  boundary, so record the entry time, not just that entry happened.
- Returns to `RUNNING` within ~60 s of the window closing (minute alarm, §1.6) —
  *not* up to an hour later, despite the code comment.
- Reaches `FINISHED` at `end_epoch`.

### H2 — Hibernating resting current

While inside the hibernation window of C5, detached, measure 300 s (feeds
**A4**). Whole minutes, per §4.

### H3 — Confirm the hibernation wake cadence

The claim in §1.6 is that hibernation wakes 60 times an hour, not once. Measure
it rather than reading the code again: with a fine statistics window, wake
events are individually visible.

```sh
embedded/tools/joulescope_measure.py --use-server --duration 300 --window 0.02
```

**Expected:** five wake events in 300 s, at :00 of each minute. One event in
300 s (or none) would mean the alarm really is hourly and §1.6 is wrong.

If per-event resolution proves awkward from the summary output, the same
conclusion follows from A4 versus A1: `HIBERNATING` drawing measurably more than
`IDLE`, by roughly one wake per minute's worth of charge, is the same evidence.

### T4 — Brownout recovery with an odd page count

Not a power test; it is the regression test for the §1.7 fix.

1. Run at `T = 10 s` until an **odd** number of headers has been written —
   `tag-info` reports the internal count, and one header lands every 60 samples,
   so 10 minutes per header. Stop the tag and confirm the count is odd.
2. Induce a brownout rather than a clean reset, so recovery takes the
   `resetBrownout` path with no monitor attached. A debug-probe reset will not
   do: it is classified as a monitor attach and resumes with `T_CONT`, which
   never reaches the round-up.
3. Let the tag run for at least two more blocks, then stop and download.

**Expected (fixed):** continuous data across the recovery, with only the partial
page lost, and no zero-sample block.

**A regression** looks like exactly one block with a header and **zero** samples
at the index equal to the pre-brownout page count, and every subsequent block's
pressure trace displaced by one block relative to its epoch.

Record which, with the page count and the downloaded block index, in either
case. If a brownout cannot be induced cleanly on this rig, say so in the report
rather than substituting a probe reset — it exercises a different path and would
pass while the defect stands.

### C6 — Optional confirmation at the deployment period

Repeat **C3** and **C4** once at `T = 90 s`, the shipped default period.
This is a spot check that nothing in the stop path is period-sensitive; entry
granularity for hibernation at 90 s is 90 minutes, which is why C5 is not
repeated here.

## 7. Phase D — data verification

Power numbers describe a tag that may have recorded nothing, or nonsense. Every
run in Phase C ends with a download, and the download is checked for **values**,
not just structure.

```sh
build-host/bin/tag-dwnld -o /tmp/prestag-<id>.db
embedded/tools/prestag_check_download.py /tmp/prestag-<id>.db \
    --period 10 --expect-duration 5400 --expect-gaps 1
```

[`prestag_check_download.py`](../../../../tools/prestag_check_download.py) exits
non-zero on failure and prints a per-check table. It reads
`Pressure(Epoch, Pressure)` in hPa, `Temperature(Epoch, Temperature)` in °C and
`Voltage(Epoch, Voltage)` in volts, one Voltage row per 60-sample block, with
sample epochs advancing by exactly `config.period()` from each block header
([pressure.cc:82-130](../../../../../host/libraries/tagcore/sqlitelog/pressure.cc#L82-L130)).

### 7.1 Structure

1. **Sample count** consistent with run duration ÷ period (`--expect-duration`).
2. **Epoch monotonicity** across the whole table.
3. **Spacing** exactly one period, with every larger step reported as a gap.
   `--expect-gaps 1` for the C5 hibernation run, `0` for the others; an
   unexpected gap is a restart that nothing else in the plan would notice.
4. **One header per 60 samples.** A mismatch is the §1.7 desynchronisation
   signature, so this doubles as the T4 check.

### 7.2 Values — why structure alone is not enough

A structural check passes happily on a tag that recorded well-formed nonsense.
Three real failures produce perfectly valid rows:

- **A failed sensor read is logged, not dropped.** `lps27GetPressureTemp()`
  presets its outputs to `SHRT_MIN` before touching the bus
  ([lps27.c:157-158](../../../common/sensors/pressure/src/lps27.c#L157-L158))
  and returns false on failure — but `state_run.c` **ignores the return value**
  and logs the sample regardless. With `lps27Pressure(raw) = raw/16` and
  `lps27Temperature(raw) = raw/100`, that reaches the database as exactly
  **−2048.00 hPa and −327.68 °C**. The checker reports those separately from an
  ordinary out-of-range value, because they mean the read failed rather than
  that the reading was wrong.
- **A stuck bus or a cached register reads the same value forever.** The
  pressure LSB is 1/16 hPa = 0.0625 hPa and real barometric pressure moves by
  far more than that over any useful window, so a run whose pressure has **one
  distinct value** did not measure pressure. Nothing structural catches this —
  it is the tidiest-looking data the tag can produce.
- **A wrong conversion or a mis-scaled raw value** stays a valid float while
  landing far outside the range air can be at.

### 7.3 Bounds

| Stream | Bound | Rationale |
| --- | --- | --- |
| Pressure | **> 900 hPa** (checker default 900–1100) | Bench ambient. The LPS27 itself is specified over 260–1260 hPa, so 1100 is a loose upper sanity bound, not a sensor limit |
| Temperature | **15–40 °C** | Room temperature. The sensor is specified −40 to +85 °C, so this is a "the bench is a room" bound |
| Voltage | 2.0–3.7 V | Below 2.00 V the firmware's own `LOGWRITE_BAT` threshold would have tripped; above 3.7 V is outside anything the baseboard supplies |
| Distinct pressure values | > 1 | See above |

Override with `--pressure-min/--pressure-max` and `--temp-min/--temp-max` if the
bench is somewhere these do not describe. Cross-check the mean pressure against
the local **station** pressure at the time — not the sea-level-adjusted figure a
weather report usually quotes, which will read ~30 hPa high at this elevation.

### 7.4 One thing not to check

The block-level `temperature` field in `PresTagLog` is **not a temperature**. It
carries a mid-block Vdd sample: `state_run.c` assigns `pState->temp10 =
pState->vdd100` with the real temperature commented out. The SQLite schema
records this deliberately — "Pressure tags use the source field for another
voltage measurement and do not write this stream"
([schema.cc:56](../../../../../host/libraries/tagcore/sqlitelog/schema.cc#L56)) —
and the SQLite writer takes the `Temperature` table from the **per-sample**
values, which are real. Read the `Temperature` table; ignore the block field.

### 7.5 Scepticism about the checker itself

A failing check is a claim about the tag and deserves the same scepticism as any
other measurement. Confirm what it actually measured before believing it — a
wrong check hides real faults behind it, which is how an intermittent download
failure went unseen on this project for a long time.

Note also that `power_experiment.check_download()` derives its expected rate from
`lsm6.odr` and returns `None` for a PresTag
([power_experiment.py:459](../../../../tools/power_experiment.py#L459)), so its
rate check **silently does not run**. That gap is why the tool above exists;
do not rely on the IMUTag path for a PresTag.

## 8. Pass/fail and baselining

On the **first** clean execution there is no regression bound to apply, only the
sanity gates:

- **`IDLE` and `FINISHED` (A1, A3, A5) below 1 µA.** This is a hard bound, not
  a "did it sleep" heuristic.
- `CONFIGURED` and `HIBERNATING` (A2, A4) above the `IDLE` floor by no more than
  one minute-alarm wake each; recorded, not gated, on the first run.
- A1 and A5 agree within 20%.
- The Shutdown fit's `I_rest` (§5.2) is below 1 µA and consistent with A1. The
  fit's intercept and the directly measured idle current are two routes to the
  same quantity, and they should agree; if they do not, one of them is wrong.
- **`I_avg` at 60 s below 1 µA**, the expected figure for a healthy tag.
- **`I_avg` at the 90 s default meets the one-year budget**: ≤ 1.256 µA on an
  11 mAh cell. On 5.5 mAh the budget is 0.628 µA and the result is expected to
  be marginal — record whether it clears, do not treat a miss as a build
  regression without comparing against the baseline.
- Every Phase C expectation in §6 met.
- Every Phase D check in §7 passed, values as well as structure.

The numbers that first run produces **become the baseline**, recorded in the
report (§9). Subsequent runs compare against it: any resting state or sweep
point moving by more than 20%, or A1/A5 diverging, is a finding to investigate
before shipping — not a number to write down and move past.

A 20% bound is tighter in absolute terms than it sounds. The whole PresTag range
is roughly 0.5–4 µA, so 20% at the 90 s default is on the order of **0.1 µA**,
and a regression that would be invisible on an IMUTag is the difference between
making and missing a year on a 5.5 mAh cell. The project has twice seen run
current move by ~200 µA on an IMUTag between builds differing only in code
layout; the lesson that carries over is that such moves happen and are found
only by measuring, not the magnitude, which on this part would be catastrophic
rather than a regression.

## 9. Recording results

Results go in [`power-test-report.md`](power-test-report.md), which carries the
tables this plan's phases fill in. Record for every session:

- git hash, **whether the tree was dirty**, and the exact target built;
- the firmware string reported by `tag-info`;
- the interpreter used for the Joulescope, and that the server was used;
- confirmation that the Joulescope desktop app and `qtmonitor` were detached;
- battery/supply voltage.

A measurement from a dirty tree is not reproducible. Say so in the report rather
than omitting it.

## 10. Deviations

### Fixed by the change that accompanies this plan

Both were the same constant, and both had to move together (§1.7):

- **Hibernation entry granularity** was 120 samples rather than 60 (§1.6), so a
  window shorter than `120 × period` could be skipped entirely.
- **The `T_INIT` round-up desynchronised the log** after a brownout recovery with
  an odd page count (§1.7), displacing every later block's samples one page from
  its header. A data-integrity defect, not a granularity wart.

**C5** and **T4** are the regression tests. Neither has been run on hardware
yet — the fix is verified only by construction and a clean build of `PresTag`
and `PresTagRaw`.

### Still open, to confirm not fix

This plan changes no further firmware. Two places where code and comments
disagree remain; each should be filed once observed:

1. **`ALARM_HOUR` behaves as `ALARM_MINUTE`** (§1.6) — identical masks in
   `enableAlarm()`. Hibernation wakes 60× more often than the comment claims.
   Confirmed or refuted by **H3**.
2. **`start_delay` is silently ignored by this family** (§1.4), so
   `tag-start --start-now` has no effect on a PresTag. Confirmed by **C2**.

### 1.2c Measured: Stop 2 is requested correctly and not entered

The delay never reaches Stop 2, and the reason is not the clock, the devices or
the debugger. A build with a 30 ms wait inserted after the LPS27 is powered off
and before the AT25 is woken — so nothing but the MCU is drawing — shows a
**flat 140 µA for the full 30 ms**. On an L432 at 2 MHz that is Sleep; Stop 2
would be 1-2 µA.

Captured by plain stores into SRAM2 at the instant of the `WFE`:

| Register | Value | Reading |
| --- | --- | --- |
| `SCB_SCR` | `0x00000004` | `SLEEPDEEP = 1` |
| `PWR_CR1` | `0x00000502` | `LPMS = 2`, Stop 2 |
| `PWR_SR2` | `0x00000100` | low-power regulator ready |
| `DBGMCU_CR` | `0x00000000` | debug-in-low-power disabled |
| `NVIC ISPR0/1` | `0`, `0` | no peripheral IRQ pending |
| **`SCB_ICSR`** | **`0x00400000`** | **`ISRPENDING = 1`** |

Every precondition for Stop 2 is met and the part sleeps shallow anyway. Ruled
out by measurement, not argument:

- **Not the debug domain.** Identical 140 µA after USB-resetting the ST-LINK
  with no session open. (Stop 2 keeps VCORE, so a held debug power-up request
  would have shown here; Shutdown removes VCORE, which is why the between-sample
  baseline is a clean 0.28 µA either way.)
- **Not device load.** The sensor rail is off and the flash is in ultra-deep
  power-down during the window.
- **Not the wrong mode.** `SLEEPDEEP` and `LPMS` are correct at the `WFE`.

**This is a regression, not a limit of the part.** Stop 2 was measured working
when `stopMilliseconds()` was first written, so something added since leaves the
core unable to enter it. The most likely shape, and the one to look for first,
is an **interrupt flag that is never cleared**: a pending source both sets
`ISRPENDING` and keeps the part out of deep sleep, and it would have arrived
with whatever code introduced it. That makes this a bisect against the commit
where Stop 2 last measured correctly, not a redesign.

The single anomaly is `ISRPENDING = 1` with an empty NVIC, which points at an
**EXTI event line** — `tagLptim1EnableWakeEvent()` sets `EMR2` for the LPTIM1
line to wake the `WFE`, and an event held in the event register makes `WFE`
return without sleeping, or sleep only shallowly. That is the lead, not a
conclusion.

**Tried and rejected: an LSI-clocked delay.** Running LPTIM1 from LSI (started
on demand, `/32` for ~1 ms ticks, with `ICR` clears confirmed before use) was
measured like-for-like at a verified 10 s period: 54 ms and **31.0 µC** against
59 ms and 34.9 µC — 11%, against ~10 µC predicted. It shortened the arming spin
but did not make the wait sleep, because shallow sleep is the real problem and
the kernel clock is not its cause. The change was reverted: it buys little,
adds a second oscillator and its ±5% tolerance, and does not address the EXTI
event lead.

**Direction.** Configure LPTIM1 once per boot, free-running, and count
autoreload ticks in the ISR rather than re-arming `ARR` and the EXTI event line
for every delay. That removes the per-delay `ARROK` synchronisation *and* the
per-delay event-line manipulation that the `ISRPENDING` evidence implicates,
and it stays on LSE. Note that at field periods each sample is a boot out of
Shutdown, so "once at init" means once per sample rather than once per run —
the arming cost falls from three times per sample to one, not to zero. At
1024 Hz one tick is 0.977 ms, so a 2 ms wait carries up to ~1 ms of phase
error; that is acceptable for the AT25 recharge and the LPS27 settling, which
are the only waits in the sample path. Whatever is implemented must be judged
by the trace: the wait plateaus collapsing from 140-165 µA to single-digit µA.

### 1.2d Defect found on the way: a stale stored configuration

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

### 1.2e Rig caveat: the debug port and Stop modes

A debugger session leaves the DP powered up until a POR or a completed
Shutdown; with it up, Stop modes keep the system domain alive and read as
Sleep-level current. `STM32_Programmer_CLI` hotplug sessions do this, and the
CLI itself wedged after repeated use until a USB reset. Measurements of Stop
depth on this rig must follow a POR, use only the host tools, and take no
debugger reads until the measurement is done. Shutdown is unaffected, which is
why the resting-state numbers stand.

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

### Not yet run

The LSI delay experiment (§1.2c) needs its all-devices-off wait to settle sleep depth.

A3, A4, A5; B1, B4, B5, B6; all of Phase C (C1–C6, T4); H3 for hibernation. The
CONFIGURED minute alarm was confirmed by trace; the HIBERNATING one was not
measured.

### Tooling found wanting, and fixed or reverted

- `joulescope_measure.py` client timeout fixed at 300 s; scaled to the window.
- `joulescope_server.py` died on `BrokenPipe` because `flush()` sat outside
  the guard; moved inside.
- Download target: `-rst` briefly replaced `-g 0x08000000`; the tag was found in
  the ROM immediately afterwards (§2.0a). Reverted to the jump; `reset=HWrst`
  and a 4-try connect retry kept.
- The instrument wedges after repeated direct open/close cycles; use the server
  for the whole session and do not run direct-driver captures alongside it.
