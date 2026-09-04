# Open Issues — Tag Firmware

Known defects that are understood well enough to write down but are not fixed.
Each entry says what the evidence actually is, so the next person can tell a
reproduced fault from one found by reading code.

Last reviewed 2026-09-04.

## Reproduced

### Flash error-flag clear cannot be put on the live low-power path

`tagPowerClearFlashErrorFlags()` clears latched `FLASH_SR` error bits and the
ECC flags before a low-power entry. It is called only from
`tagPowerEnterStop3()`, which carries `__attribute__((unused))`. The live
terminal path is `tagPowerEnterStandby()`, reached through
`tagPowerEnterTerminalSleep()`, and the live idle path is
`tagPowerEnterIdleMode()`. **The clear runs on neither.**

Two attempts to add it, each measured with `tag_lifecycle_check.py` on
IMUTagNandBmp581:

| build | idle / stopped / idle again |
| --- | --- |
| baseline | 4.94 / 4.94 / 4.94 uA |
| clear on both live paths, unconditional | 1036 / 1038 / 1038 uA |
| clear only when a flag is actually latched | 1037 / 1037 / 1037 uA |
| reverted to baseline | 4.94 / 4.94 / 4.94 uA |

The second result is not credible as a mechanism: with no flags latched that
version only reads `FLASH_SR`, `FLASH_ECCCR` and `FLASH_ECCDR` and writes
nothing, and three register reads cannot cost 1 mA. So either the bisect is not
as clean as it looks or something incidental to the added code matters. Needs a
proper investigation with a control build, not another two-shot attempt.

This matters because several documents, and `AGENTS.md`, used to present this
function as the first thing to suspect when a tag reports IDLE at run current.
Those have been corrected; the measurements they cite (6.705 uA) were taken
when Stop3 was the live path and are not reproducible today.

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
