# Open Issues — Tag Firmware

Known defects that are understood well enough to write down but are not fixed.
Each entry says what the evidence actually is, so the next person can tell a
reproduced fault from one found by reading code.

Last reviewed 2026-09-04.

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

The rule that replaces this entry is in `AGENTS.md` under "The Standby arming
window is off limits", and at the `SLEEPDEEP` write in `pwr-u375.c`.

What remains true and unfixed: `tagPowerClearFlashErrorFlags()` is called only
from `tagPowerEnterStop3()`, which is `__attribute__((unused))`, so it runs on
no path the tag takes.

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

### REVERTED: the write-error page skip caused a 240x idle regression

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

### Still open: a stop request can go unserviced for at least 30 s

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

- Rework `eraseExternal()` liveness. `chThdYield()` is slow and expensive; it
  should check for any pending event, push it back to the outer loop and
  return. The model is: while no event, erase; if an event is pending, return.
- Consider a full erase sweep when a run did not finish with clearly
  recoverable boundaries.
