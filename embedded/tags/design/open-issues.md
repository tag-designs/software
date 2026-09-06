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

### Write errors are reported to the host as "external log full"

`state_run.c` maps both `LOGWRITE_FULL` and `LOGWRITE_ERROR` to
`IMU_BLOCK_EXTERNAL_FULL`, at four call-site pairs (lines 292/295, 343/349,
362/368, 395/401), which reaches the host as `EVENT_EXTERNALFULL`. The enum
comment concedes it: *"External NAND storage is full or failed."*

A tag that cannot write is therefore indistinguishable from one that filled up.
This actively misled an investigation: writes refused because the GD5F powers
up with block protection enabled were reported as a full log. Splitting the two
is agreed; it is not done.

### Intermittent non-monotonic timestamps under attach storms

One 30-cycle attach storm produced 4 backwards `ElapsedUs` steps in 7050 rows.
An identical repeat produced none. An undisturbed run is clean, and `ElapsedUs`
normally continues monotonically straight across `RESTART_RECOVERY` segment
boundaries -- so this is an anomaly in restart-recovery timestamping, not an
expected consequence of resetting a tag mid-run.

`tag_attach_storm.py` checks for it and will fail when it recurs. The check is
deliberately left strict.

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
