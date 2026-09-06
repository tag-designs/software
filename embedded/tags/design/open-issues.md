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

### Unexplained: code in the Standby arming window stops Standby

Any code added to `tagPowerEnterStandby()` can stop the part entering Standby.
The core reaches the WFI and never returns, drawing about 1035 uA instead of
4.4 uA. It does not reset and takes no fault -- a marker written either side of
the WFI shows the pre-WFI store executed and the post-WFI store did not, and no
fault handler was entered.

It is erratic rather than proportional:

| perturbation before the WFI | result |
| --- | --- |
| 0-11 no-op instructions | all sleep (12 builds) |
| 1-2 flash reads | sleeps |
| 4 flash reads, straight-line or looped | stalls |
| 8 flash reads, straight-line | sleeps |
| 4 SRAM reads | stalls |
| a 24th scan region that reads nothing | stalls |
| a 24th table entry that is not scanned | sleeps |
| ~30 bytes of logging at the top of the function | stalls (3 of 3 builds) |

At the instant of the stalled WFI, **no register differs from a working
build**: 0 differences across 149 non-default addresses covering PWR, RCC with
every STPENR and SLPENR, SCB, NVIC, RTC, TAMP, EXTI, FLASH, I2C1, SPI1, TIM2,
GPIOA-H, GPDMA1, ICACHE, RAMCFG, DBGMCU and DHCSR/DEMCR. Eliminated by direct
measurement: latched flash/ECC flags, SRAM2 parity (disabled in the option
bytes), pending interrupts, DMA activity, autonomous clock requests, ICACHE,
debug vector catch, and build non-determinism (the `.list` is reproducible).

Nothing in production occupies that window, so nothing is broken today. It
matters only because it makes the path impossible to instrument, and because it
produced a long series of confident, wrong diagnoses before the pattern was
recognised. A minimal reproducer for ST would be the 24th-region case: a loop
iteration that performs no memory access at all, added before the WFI.

### Unexplained: SRAM2 page 3 is not reliably retained across Standby

`PWR_CR1_RRSB3` is supposed to retain the last 8 KB of SRAM2 through Standby,
and `tagScratchRetain()` sets it. The contents survived once and have not
since. At the point of the write, `RCC_AHB1ENR2` reads `0x00000004`, so PWR is
clocked, and `PWR_CR1` reads back `0x40` with the bit set. The page is still
lost. Setting the bit at boot behaves the same way.

This does not block the scratchpad's main use, which is reading a log back from
a tag that failed to sleep, crashed or wedged -- those never lose SRAM, and the
region survives the reset that reading it causes.

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
