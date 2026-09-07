# Open Issues — Tag Firmware

Known defects that are understood well enough to write down but are not fixed.
Each entry says what the evidence actually is, so the next person can tell a
reproduced fault from one found by reading code.

Last reviewed 2026-09-07.

## Reproduced

### RESOLVED: the flash error-flag clear was never the problem

Previously filed here as "adding `tagPowerClearFlashErrorFlags()` to the live
low-power path costs 1 mA, and three register reads cannot cost 1 mA, so the
bisect must be wrong". The bisect was right and the reasoning was wrong: the
call had been placed **between the LPMS/SLEEPDEEP writes and the WFI**, and
anything placed there stops the part entering Standby. It had nothing to do
with flash.

That was established by putting a probe in the same window that touches no
flash at all. Reading a single core register there stalls Standby; so does a
scan region that reads nothing; so does thirty bytes of logging. Meanwhile the
flash error flags were captured directly in a failing build and were clean --
`FLASH_SR`, `FLASH_ECCCR` and `FLASH_ECCDR` all zero, identical to a build that
slept.

The rule that replaces this entry is in `AGENTS.md` under "Terminal sleep is
Stop 3; Standby entry is layout-sensitive", and at the `SLEEPDEEP` write in
`pwr-u375.c`.

Since 0638a76 the terminal sleep *is* `tagPowerEnterStop3()`, so
`tagPowerClearFlashErrorFlags()` now runs before every terminal sleep. The
1036 uA once measured when it was added to the live path was the layout stall,
not a cost of clearing flags; the Stop 3 tree that includes it passed the
release gate at 8 uA.

It does not belong in the power path at all. Clear the flags **where the
failure occurs -- in the datalog code**, at the flash operation that latched
them. That is where an ECC or program error is meaningful, where it can be
attributed to a specific access, and where it can be reported rather than
silently discarded. Clearing them blanket-fashion before sleep hides the error
and, as this entry records, cost two days on the assumption that the sleep path
was the problem.

### RESOLVED: the Standby arming window was never the problem

Previously filed here as "any code added to `tagPowerEnterStandby()` can stop
the part entering Standby", with a table of erratic perturbations -- four flash
reads stall, eight sleep, a scan region that reads nothing stalls, thirty bytes
of logging stalls 3 of 3 -- and no register differing between a working and a
failing build across 149 non-default addresses.

Every one of those observations was real. The conclusion drawn from them was
wrong. The perturbations were not doing anything *at the WFI*; they were
changing the **layout of the image**, and Standby entry was a lottery decided
by that layout.

This was established with perturbations that provably cannot change behaviour.
Inserting `n` `nop` instructions at the top of `Reset()` in `state_machine.c`
-- a function not even on the idle path, in a build where the erase sweep has
no work to do -- flips idle current, three trials at each point:

| padding | idle current |
| --- | --- |
| none | 5.18 uA |
| 2 nops | 5.21 uA |
| 4 nops | 1040 uA |
| 8 nops | 1040 uA |
| 16 nops | 1040 uA |

The standby `WFI` is at the *same address* in the sleeping and the stalling
builds. What moves is everything else: the build uses LTO, and left alone the
partitioner inlines `tagPowerEnterStandby()` all the way into `main()`, which
places the arming sequence and its `WFI` inside a ~2.5 KB function whose
literal pool sits past the `WFI`.

**The fix is `__attribute__((noinline))` on `tagPowerEnterStandby()`**, so the
arming sequence stays a small self-contained function. Verified over three
trials at each of nine image layouts across two independent padding sites
(inside `Reset()`, and inside `main()`), against a baseline that fails at five
of them.

What was tested and does **not** fix it, each against the padding point that
reliably fails:

| attempted fix | result |
| --- | --- |
| `__attribute__((aligned(16)))` alone | still stalls -- inert, since the function is inlined and has no entry point to align |
| clearing `FLASH_ACR_PRFTEN` | still stalls |
| moving the arming sequence into SRAM via `.ramtext` | **every** image stalls, including the pristine one |
| disabling ICACHE | **every** image stalls, including the pristine one |

Two unrelated ways of preventing the inline both cure it: `noinline`, and
`optimize("O0")` on the same function. That is the evidence the inlining is the
operative variable rather than a coincidence of one attribute.

**Still unexplained:** *why* the inlined form fails. The arming sequence
disassembles correctly in both cases -- `PWR_CR1.LPMS=4`, `SCB_SCR.SLEEPDEEP`,
`DSB`, `ISB`, `WFI`, in that order, with no reordering -- and the earlier
register comparison found nothing different at the stalled `WFI`. A reproducer
for ST would be the `nop` sweep above: identical source, identical `WFI`
address, 200x difference in idle current.

### RESOLVED: SRAM2 page 3 is retained across Standby

`PWR_CR1_RRSB3` retains the last 8 KB of SRAM2 through Standby, and
`tagScratchRetain()` sets it from the top of `tagPowerEnterStandby()`. This was
previously filed as unreliable -- the contents survived once and not since.

It works. A/B at one commit, `TAG_SCRATCHPAD=1`, three reset/standby/wake
cycles each, with idle current measured on every cycle to prove the part
actually slept:

| build | idle | page after three Standby cycles |
| --- | --- | --- |
| `tagScratchRetain()` armed | 5.37 uA | magic `0x33524353`, `seq=4`, record intact |
| retention not armed (control) | 5.15 uA | magic `0x8F2B4AA3` -- noise |

The control is the part that makes this conclusive: it shows Standby really
does power the page down, so survival in the armed build is the retention bit
working rather than the page never having been at risk.

The earlier "worked once and not since" result was entangled with the Standby
entry fault: a build that stalled at the WFI never lost SRAM, so the page
survived trivially and the test read as a pass without ever exercising
retention. Re-running it only became meaningful once Standby entry was
deterministic.

### RESOLVED by Stop 3: the Standby request is declined by layout

**Resolution (2026-09-07, commit 0638a76):** the terminal sleep now goes
through Stop 3 (`tagPowerEnterStop3()`, RTC wake through WKUP7, synthetic
standby reset on wake). It entered at every layout that stalls Standby and
passed `tag_release_check.py`; cost about 3.6 uA at rest. *Why* the Standby
request is declined remains unknown -- see the two subsections below for what
was established and excluded -- and `tagPowerEnterStandby()` stays in the tree,
unused, as the reference. Everything from here to the next entry is the record
of that search, kept so it is not repeated.

#### The compiler search, exhausted

Standby entry depends on where code lands in the image, and no build setting
short of whole-tree `-O0` makes it deterministic. Whole-tree `-O0` cannot
sustain 1600 Hz, so it is not available. Recorded here so nobody repeats these
builds.

Three trials per cell. "skip" is the `state_run.c` change from the reverted
commit below, which reliably discriminates; "padding" is four inert `nop`s in
`main()`.

| configuration | no skip | + skip | + padding | 1600 Hz |
| --- | --- | --- | --- | --- |
| `-O2` + LTO on (shipping) | sleeps | **stalls** | **stalls** | ok |
| `-O2`, LTO off | **stalls** | **stalls** | -- | -- |
| `pwr.o` at `-O0`, LTO on | **stalls** | sleeps | sleeps | -- |
| `pwr.o` at `-O0`, LTO off | **stalls** | **stalls** | **stalls** | -- |
| whole tree `-O0`, LTO off | sleeps | sleeps | sleeps | **cannot keep up** |

The `pwr.o at -O0` rows are the point: flipping LTO inverts which variants
stall without reducing the sensitivity. Partial measures move the lottery, they
do not end it.

Also tried and rejected, each against a padding point that reliably fails:
`__attribute__((aligned(16)))`, which is a no-op because `-falign-functions=16`
is already in `USE_OPT`; relocating the arming sequence into SRAM via
`.ramtext`, which makes **every** image stall; and disabling ICACHE, likewise.
`noinline` on `tagPowerEnterStandby()` narrows the failure surface -- it
survived nine deliberately varied layouts -- but does not close it, as the
reverted commit below proves.

What follows from this is a process, not a fix: **measure idle current on the
image being shipped.** `embedded/tools/tag_release_check.py` does that along
with the life-cycle walk and attach storms. The failure is silent, 240x, and
survives every functional test, so the only defence is to look.

#### What the stall is, and what it is not (2026-09-07)

A day with OpenOCD, the scratchpad and the Joulescope established the shape of
the fault precisely, without finding its cause. Recorded so the same ground is
not covered again.

**The firmware reaches the `WFI` and never returns.** A scratchpad record
written immediately before the Standby `WFI` is present after a stall; one
written immediately after it is absent. `godown()` is not refusing: probes at
both of its early exits stay silent in a stalling boot. The stall occurs in
boots with **no monitor session and no debugger** -- a plain programmer reset
is enough.

**The stalled state is Sleep with the bus clocks running.** Current is flat at
1040 uA in every 0.2 s block for minutes -- not a duty cycle. Gating TIM2's APB
clock just before the `WFI` lowers the stalled current by 42 uA. Datasheet
Stop 0 is ~170 uA. The deep-sleep request is declined and the `WFI` degrades to
an ordinary sleep that nothing wakes.

**The state one instruction before the `WFI`, sampled live in a stalling
boot** (plain stores to retained SRAM2, no debugger): `PWR_CR1 = 0x44`
(LPMS = Standby, RRSB3), `SCB_SCR = 0x4`, `PWR_SR = 0`, `PWR_WUSR = 0`,
`PWR_VOSR = 0x01010101` (R1EN, BOOSTEN, R1RDY, BOOSTRDY), `RCC_CR = 0x1F`
(MSIS/MSIK ready), `FLASH_SR = 0` with no programming in the boot,
`NVIC_ISPR = 0`, `SCB_ICSR = 0`, no EXTI/RTC/I2C flags, `DHCSR.C_DEBUGEN = 0`,
`DBGMCU_CR = 0`. Every precondition in RM0487 Table 93 is met.

**Failing and working images are bit-identical at the `WFI`** across ~380
registers -- PWR, RCC (every ENR/SLPENR/STPENR/CCIPR), SCB, NVIC, SysTick,
EXTI, RTC, TAMP, FLASH, ICACHE, SYSCFG, GPIOA-H, SPI1, I2C1, LPTIM1, ADC1, CRS,
DBGMCU, DWT/FPU -- captured at a hardware breakpoint on each image's `wfi`.
Only the core GPRs, the RTC time, one backup-register counter and `FPCAR`
differ. ICACHE hit/miss monitors across the `WFI`: hits only, in both.

**Tested on a reliably failing layout, three or more trials each, no effect:**
`WFI` at the start of a 16-byte flash line with `nop` padding after it (the
STM32U5 errata workaround shape); a PWR register read-back before the `WFI`;
TIM2 stopped and unclocked; every LPTIM reset through RCC before the `WFI`
(ES0626 2.11.1); `FLASH_ACR_PRFTEN = 0`; a bounded wait for `C_DEBUGEN` to
clear; a host-side delay before `STLINK_DEBUG_EXIT`; a non-blocking main loop
outside RUNNING; file-scope `-O0` on the bus drivers with LTO off, with and
without `noinline` on their teardown routines; a dedicated linker section for
the power code. ES0626 Rev 3 has no item on Standby entry. The `*STPENR`
registers cannot be cleared (fixed bits read back), so "peripheral clock in
Stop" was never actually removed as a variable.

**Two things that looked like causes and were not.** A stream of `godown()`
return records in an early capture read as a monitor-attached spin; it was the
one second the host was attached, followed by the silent stall. And the
`WFI` returning after ~1 s under the debugger, on both images, was a hardware
breakpoint on the following instruction -- a debug event -- not the fault.

A forum post with these numbers is `stm32u375-standby-forum-post.md` in this directory; the practical answer
remains the release gate above.

#### Stop 3 as the terminal sleep: measured, not yet adopted (2026-09-07)

`tagPowerEnterStop3()` -- the same preparation, `LPMS = 011`, RTC wake through
WKUP7 (`WUSEL7 = 11` selects RTC_ALRA/ALRB/WUT/TS), and `NVIC_SystemReset()`
on wake with `resetCause = resetStandby` -- was wired in as the terminal sleep
and put through the layouts that stall Standby:

| layout | Standby | Stop 3 IDLE | Stop 3 FINISHED |
| --- | --- | --- | --- |
| base (main + non-blocking loop) | IDLE 4.6, **FINISHED 1039** | 8.07 | 8.07 |
| + skip | **1040** | 8.02 / 8.12 | 8.14 |
| + skip + 4 nops | **1040** | 8.08 | -- |
| + skip + 16 nops | **1040** | 8.05 / 8.14 | 8.13 |

A scheduled start (`start_delay: 2`) parked at 7.95 uA and woke on the 12:23:00
minute alarm into an 810 uA run, as the state machine predicts. The cost is
about 3.6 uA at rest against a Standby that works. Whether to ship it is a
decision, not a measurement; the `tag_release_check.py` run on that tree is
the evidence to decide on.

### REVERTED, then re-landed under Stop 3: the write-error page skip

**Re-landed 2026-09-07.** The revert below was never about the code: the change
moved the image into a layout at which the Standby request is declined, and the
same `state_run.c` is one of the "skip" layouts in the tables above. With the
terminal sleep now through Stop 3 that reason is gone. Re-verified on the
Stop 3 tree: a test build with `IMUTAG_TEST_PAGE_ERROR_EVERY=25` ran 58 s at
400 Hz through six injected page failures -- six `RESYNC_STORAGE_SKIP` events,
seven segments, 23100 accelerometer rows, download sane -- and the shipping
build (injection compiled out, `#warning` absent) idled at 8.04-8.11 uA with a
passing life-cycle walk. The record of the original revert is kept below.

The change described below worked, was tested on hardware, and was reverted
because it stopped the tag sleeping.

`skipFailedExternalPage()` in `state_run.c` let a run survive a bad page and
announced the discontinuity with `RESYNC_STORAGE_SKIP`. Verified with injected
failures: at one page in twenty the run stayed RUNNING and produced five skip
events over six segments; at every page it ended with `EVENT_STORAGEERROR` at
the cap. None of that was wrong.

What was never measured is idle current, and the commit landed with a **1033 uA
idle against 4.34 uA before** -- the Standby stall, on a code path idle never
executes. Bisected to the commit, then within it:

| build | idle |
| --- | --- |
| before the commit | 4.40 uA |
| + the proto enum alone | 4.38 uA |
| + proto and `state_run.c` | 1036 uA |
| same, fault injection stripped out | 1036 uA |
| `state_run.c` reverted | 4.33 uA |

So the trigger is the presence of the skip code, not the injection and not the
proto change. `tagPowerEnterStandby()` still carries `noinline` and sits at the
same address, `0x08002260`, in both the sleeping and the stalling image, so the
standby function did not move -- consistent with the residency mechanism, where
what matters is the rest of the image around it.

**`noinline` narrows this failure surface but does not close it.** It survived
nine deliberately varied image layouts, which was taken as a fix; a real change
elsewhere in the tree brought the stall back. The entry above that calls the
Standby window resolved should be read with that in mind.

The feature is worth re-landing. It needs the layout problem understood first,
not another roll of the dice, and an idle measurement attached to the commit --
which AGENTS.md already required and this commit did not do.

### RESOLVED: the non-monotonic timestamps were a bug in the check

Filed as "one 30-cycle attach storm produced 4 backwards `ElapsedUs` steps in
7050 rows, an identical repeat produced none", and left strict on purpose.

The tag was never wrong. `check_download()` in `embedded/tools/power_experiment.py`
chose its timestamp column by walking the table's columns in declaration order
and taking the first recognised name. In `ImuAccel` that is `RawElapsedUs`,
which comes before `ElapsedUs`, and `RawElapsedUs` is documented in
`host/libraries/tagcore/sqlitelog/README.md` as *uncorrected elapsed
microseconds from the segment start*. It restarts at zero in every segment by
design, so it steps backwards once per segment boundary.

On one kept database, checked both ways:

| column | backwards steps |
| --- | --- |
| `RawElapsedUs` (what the tool measured) | 5 |
| `ElapsedUs` (the corrected series) | 0 |

Every step was exactly a segment boundary, and the count always equalled the
number of `RESTART_RECOVERY` events -- 5 recoveries, 5 steps. The corrected
series ran straight through: `0..5997500`, then `11407000..14404500`, then
`33016000..36013500`, and so on.

This also explains the two observations in the original note that did not fit a
firmware fault. An undisturbed run was "clean" because it has a single segment
and therefore no restarts. And `ElapsedUs` really does continue monotonically
across `RESTART_RECOVERY` boundaries -- that was checked by hand and was
correct; it simply was not the column the tool was reading.

Fixed by selecting the timestamp column from an explicit preference list, most
corrected first, with segment-relative columns last.

Two things were ruled out along the way with the retained scratchpad, and are
worth not re-chasing:

- **The RTC is not read stale on recovery.** `restartDataCollectionClock()`
  re-bases each segment from the wall clock. Recording the epoch and
  millisecond actually used, across 625 recoveries in three storm sets that all
  reported failures, the base was strictly increasing every time.
- **`restoreLog()` is not rewinding the write cursor.** It ran on 2 of 45 boots
  in a storm round that produced 5 recoveries, so it is not on the recovery
  path at all.

### RESOLVED (host side): the intermittent download failure

`tag-dwnld` failed intermittently after a storm round with "Can't dump logs
from current state". The download was right to refuse: the tag really was still
RUNNING. The fault was in `tag-stop`.

`Tag::Stop()` returns true when the request is *acknowledged*, and the monitor
handler only does `*work |= MON_WORK_STOP` -- the state machine makes the
transition later. `tag-stop` read the status immediately afterwards, saw
RUNNING, printed it and exited 0, reporting a stop that had not happened.
`tag-reset` already polls for IDLE after `Erase()` for exactly this reason;
`tag-stop` never got the same treatment.

It now polls for FINISHED or ABORTED at two-second intervals -- each poll is a
monitor request the tag must service, so hammering the link competes with the
work being waited on -- and fails with the last state seen.

**A second cause, found 2026-09-07 (a71752f):** the same message also came
from `tag-dwnld` itself, on a tag that *was* FINISHED, with an immediate retry
succeeding. `tag-dwnld` read the status once after attaching, and attach
connects under reset, so that read could be STATE_UNSPECIFIED while the tag
booted. It now settles like the other tools, and with `--stop` polls for
FINISHED instead of reading once after a posted `Stop()`. With that, every
`tag-*` tool that judges the tag's state waits for a definite one first.

### RESOLVED (host side): STATE_UNSPECIFIED after reset

Filed as three of twenty reset-and-set-clock cycles reporting
`STATE_UNSPECIFIED` rather than `IDLE`.

`Tag::Attach()` connects under reset, so the tag is still booting when the
host's first request arrives, and `pState->state` is legitimately zero until
the state machine restores it. `tag-reset` issued `GetStatus` immediately after
attach and took that transient zero as the tag's state: it matched neither
RUNNING/HIBERNATING nor FINISHED/ABORTED, so the erase was skipped and it
printed "Final state: STATE_UNSPECIFIED". The `SetRtc` issued next hit the same
unsettled tag and was rejected, which is the neighbouring "SetRtc failed".

The tag was never at fault. At a captured failure the retained state was
healthy -- `valid` was BACKUP_STATE_VALID_MAGIC and `state` was IDLE -- the
tag's own `run_diag` reported `state=2` throughout, and `statusDiagWriteFast()`
never fired, so the fast status path never saw a zero either.

`tag-start` already knew this; its comment says attach needs a round trip to
settle. `tag-reset` now polls for a definite state after attach, at one-second
intervals, with `--settle-timeout`. Twenty rapid reset cycles then ran clean
against one to four failures per ten before, and 60 clock cycles across six
storm sets ran 60/60.

### RESOLVED: a stop request could go unserviced for at least 30 s

**Fixed in 928093d (2026-09-07):** `MON_WORK_ALL` is now in the RUNNING
state's event-wait mask, so a posted stop wakes the main thread on its own.
Qualified twice with `tag_release_check.py`; the storm failures on those runs
were host-tool settle races, fixed in 43119cb and a71752f. The original
analysis follows.

With `tag-stop` waiting properly, one storm set in six failed with "Tag did not
reach a stopped state within 30 s; last state RUNNING". The tag genuinely did
not act on the stop. This was previously invisible: `tag-stop` exited 0 and the
symptom surfaced later as a confusing download refusal.

The suspect is the event wait for the RUNNING state in `main.c`:

```c
eventmask_t wait_events = EVT_HARDWARE_ALL;
if (isMonitorEnabled())
  wait_events |= EVT_MONITOR_ALL;
pending_events = chEvtWaitAny(wait_events);
```

`MON_WORK_ALL` is not in the mask, so a posted `MON_WORK_STOP` cannot wake the
main thread on its own. It is collected by the
`chEvtGetAndClearEvents(MON_WORK_ALL)` at the top of the loop, but only once
something else has woken it -- normally the monitor request's own
`EVT_MONITOR_*`, which is masked in only while `isMonitorEnabled()` is true.

Not proven. At 400 Hz the IMU should wake the loop far sooner than 30 s, so
either that wake path is also blocked or `isMonitorEnabled()` is false at that
moment. Reproduced once in six sets. Adding `MON_WORK_ALL` to the mask is the
obvious change, and it belongs to the state-machine path that AGENTS.md says
must be measured rather than argued.

## Found by reading code, not reproduced

None of these is known to cause a current symptom.

- **`gd5fSectorErase()` reports success without erasing** on three paths:
  logical block out of range, mapping failure, and physical block out of range.
  A caller cannot distinguish "erased" from "silently skipped".
- **`gd5fRead()` does not invalidate `gd5f_cache_active`.** The flag is cleared
  only in `gd5fProgramCacheLoad()` and `gd5fProgramExecuteCache()`, so a read
  loads a different page into the device cache register while the flag still
  claims the programmed page is resident.
- **Unbounded hardware waits** in the IMUTagNandBmp581 RTC LLD:
  `hal_rtc_lld.c` lines 631 (`ALRAWF`), 648 (`ALRBWF`), 727 (`WUTWF`), and the
  `do`/`while` at 576 inside a critical zone. Any of them hangs the tag if the
  bit never sets.
- **Asymmetric wakeup-timer disable.** `rtcSTM32SetPeriodicWakeup()` waits for
  `WUTWF` when arming but not when disarming. Investigated and **exonerated**
  as a cause of the idle fault -- compiling the wakeup timer out entirely did
  not change it -- but the asymmetry is still there. Note that the IMUTag
  family never calls `enableTicker()`, so this target never arms the timer.

## Agreed work, not started

- ~~Rework `eraseExternal()` liveness.~~ Done, in two parts. The model --
  while no event, erase; if an event is pending, return -- is what `Reset()`
  in `state_machine.c` implements around the incremental
  `eraseExternalNextSector()`, testing the pending-event mask after every
  sector. The blocking `eraseExternal()` in the IMUTag `datalog.c` is dead on
  this target; its `chThdYield()` was removed on 2026-09-07 and the function
  documented as the synchronous sweep it is. The shipped image is byte-identical
  before and after, so no measurement was owed.
- Consider a full erase sweep when a run did not finish with clearly
  recoverable boundaries.
