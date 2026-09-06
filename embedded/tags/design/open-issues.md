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

### RESOLVED: a write error now skips the page instead of ending the run

Filed as "`state_run.c` maps both `LOGWRITE_FULL` and `LOGWRITE_ERROR` to
`IMU_BLOCK_EXTERNAL_FULL`, so a tag that cannot write is indistinguishable from
one that filled up". It had already misled one investigation: writes refused
because the GD5F powers up with block protection enabled were reported as a
full log.

Splitting the two was the agreed fix, but ending the run on a write error is
itself the wrong response. A single bad page cost the rest of a deployment. The
four external `LOGWRITE_ERROR` sites now call `skipFailedExternalPage()`, which
advances the log cursor past the page that failed and keeps collecting. Each
write is already retried once before it gets there, so this handles persistent
failures, not transient ones.

The discontinuity is announced with `IMUTAG_HEADER_RESYNC` and
`IMUTAG_HEADER_RESYNC_STORAGE_SKIP`, which the next sparse checkpoint records.
Markers stay on the existing `IMUTAG_CHECKPOINT_PAGES` cadence rather than
being forced out early, the same way `restoreLog()` announces
`RESTART_RECOVERY`. Both flags were already defined and already decoded by
`host/libraries/tagcore/sqlitelog/imutag.cc`, and had simply never been set by
firmware, so no host or schema change was needed.

Both flags are set: `next_header_flags`, which is what the checkpoint writer
actually reads, and the retained `checkpoint_flags_pending`, so the marker
survives a reset landing between the skip and the checkpoint. Setting only the
retained copy -- which is all `restoreLog()` does, because a restart always
follows it and reloads the other -- silently wrote no marker at all.

After `IMUTAG_MAX_CONSECUTIVE_PAGE_ERRORS` (4) failures in a row the device is
treated as unwritable and the run ends with the new `EVENT_STORAGEERROR`, which
is additive to the proto. `EVENT_EXTERNALFULL` now means only what it says.

Verified on hardware with `IMUTAG_TEST_PAGE_ERROR_EVERY`, a compile-time
injection that is byte-identical to absent when unset and emits a `#warning`
when set:

| injected failure rate | outcome |
| --- | --- |
| every 20 pages | stayed RUNNING, 16950 rows, 5 `RESYNC_STORAGE_SKIP` events over 6 segments, ended `EVENT_STOPCMD` at 113 pages |
| every page | ended `EVENT_STORAGEERROR` at 4 pages, as the cap intends |

Skipping never erases the failed page: erasing a factory bad block destroys its
marker permanently.

Not addressed: the internal-checkpoint error path still reports
`IMU_BLOCK_INTERNAL_FULL`. Internal flash filling is a genuine end-of-run, but
an internal *error* is conflated with it in the same way this entry describes.

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

### Still open: intermittent download failure after a storm round

With the false timestamp failure removed, a real one became visible that it had
been masking: `tag-dwnld` occasionally fails after a storm round. Seen in one
round of each of two consecutive two-round sets, then not at all in the next
three rounds, so it is genuinely intermittent and not yet characterised. It was
invisible before because `check_download()` returned "fail" on the bogus
monotonicity check first, so the round was already marked failed.

### Still open: STATE_UNSPECIFIED after reset

Three of twenty reset-and-set-clock cycles left the tag reporting
`STATE_UNSPECIFIED` rather than `IDLE` in one baseline session; later sessions
ran 10/10 and 4/4 clean. Also intermittent, also not characterised.

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
