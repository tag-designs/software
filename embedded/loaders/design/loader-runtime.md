---
type: design
status: current
summary: The external loader contract as traced on hardware and the runtime rules it imposes: no startup code, interrupts or OSAL sleeps, rescue erase, Serve().
---

# Loader Runtime Design

Status: implemented for `AT25XE_PresTagv3` and validated on hardware
(2026-09-30). This document records the contract a loader runs under, the rules
that follow from it, and the reasons for each, so that the next loader starts
from what is known rather than rediscovering it. The orientation and the
add-a-loader checklist are in the [loaders README](../README.md); the case for
loaders at all is in [Field Data Extraction](../../tags/design/proposals/field-data-extraction.md).

## The contract

STM32CubeProgrammer's external-loader interface is not formally specified. What
follows comes from ST's reference loaders and from `-vb 3` traces of this
loader on a PresTag.

**The descriptor must be at address 0.** CubeProgrammer finds `StorageInfo` by
symbol, but only accepts it in its own load segment at address 0, 200 bytes
long (`sizeof(struct StorageInfo)`). ST's SPI-NOR loaders
(`MX25L4006E_STM32WL5M-SUBG`, `MX25R3235F_STM32WBA25E-NUCLEO`, shipped in
CubeProgrammer's `ExternalLoader/` directory) are laid out this way, as is the
`Info` region of ST's IAR link maps. The first build placed `StorageInfo` in
SRAM with the rest of the image. CubeProgrammer then accepted `-el` without a
word, did not load the loader, and served reads of `0x90000000` directly over
SWD -- zeros on an STM32L432, reported as `Data read successfully`. Nothing in
the output distinguishes this from a working loader except the `-vb 3` trace.

**How an entry point is called**, from the trace:

| Step | Detail |
| --- | --- |
| Download | The loadable SRAM segment is written at its link address, `0x20000004`. The descriptor segment is not downloaded. |
| Return trap | `0xBE00` (`BKPT`) is written at `0x20000000`, and LR is set to `0x20000001`. An entry point returns into a breakpoint, and the programmer sees the core halt. |
| Stack | MSP is set just past the image: `0x2000090C` for a 0x50C-byte image. About 1 KB. |
| Buffer | The transfer buffer follows the stack (`0x20000940`). |
| Arguments | R0-R2 as in the C signature, every other register zeroed. Result in R0: 1 success, 0 failure. |
| Sequence | `Init` is called before each operation, not once per session. |

The entry points are `Init`, `Read`, `Write`, `SectorErase(start, end)` and
`MassErase(parallelism)`. `SectorErase`'s end address is treated as an address
within the last sector, as ST's loaders do. `Verify` and `CheckSum` are
optional and not implemented; CubeProgrammer verifies with `Read`.

**Addresses are fictional.** The flash is on plain SPI and is not memory
mapped. The loader declares a base of `0x90000000`, the conventional
external-memory address, and subtracts it.

## Runtime rules

Each rule exists because the obvious alternative fails.

**No startup code.** There is no `crt0`, no vector table and no reset handler:
the programmer jumps straight to an entry point. `make.mk` takes only the
include paths from ChibiOS's startup makefile. `Init` clears `.bss` itself,
between linker symbols, and `.data` is simply downloaded.

**Interrupts are disabled on entry and left disabled.** On a hot attach the
tag's interrupts are still armed, and one taken during an entry point would run
tag firmware from internal flash, since VTOR still points there. ST's loaders
disable interrupts in the same way.

**No kernel, and no OSAL sleeps.** The os-less OSAL's `osalThreadSleep` waits on
a virtual timer advanced by the SysTick interrupt, so with interrupts off it
never returns; its `osalSysPolledDelayX` is an empty stub. Moving a driver from
`chThdSleep*` to the OSAL does not make it loader-ready. Delays come from the DWT
cycle counter (`loader_delay.c`), which needs neither.

**Every wait is bounded.** The clock switch, each SPI byte and each flash
status poll has an iteration or time limit, and a timeout returns failure.
A loader that hangs holds the programmer until it gives up, with nothing to say
why.

**The clock is set by hand, and ChibiOS's clock setup is never called.** The
loader switches SYSCLK to HSI16 (16 MHz), which puts SCK at 8 MHz. That switch
is about eight register writes (`loader_clock.c`), touching only `RCC_CR`,
`RCC_CFGR` and `FLASH_ACR`. ChibiOS's `stm32_clock_init()` is ruled out
because it:

- resets every peripheral;
- opens backup-domain write access and leaves it open;
- calls `bd_reset()`, which resets the **whole backup domain** -- the RTC and
  the backup registers holding `pState` -- whenever the configured RTC clock
  source differs from the tag's. With LSE left off, as it must be here, it
  always differs.

`halInit()` is not called either. `mcuconf.h` sets `STM32_NO_INIT TRUE` as a
guard, and describes HSI16 only so that compile-time constants such as
`STM32_SYSCLK` are true; `loader_clock.c` refuses to compile if they are not.
The loader may be entered with the part in any clock state: MSI at 4 MHz after a
reset, or the tag's 2 MHz MSI in voltage range 2 on a hot attach. HSI16 is within
range 2's limit, so the voltage range is left alone. Flash latency is only ever
raised, never lowered.

**Only PAL is used from the HAL.** `halconf.h` enables PAL alone, and
`make.mk` compiles one ChibiOS source, `hal_pal_lld.c`, for `palSetLineMode()`.
The board description comes from the committed `board.h`; `board.c` is not
built, because its `__early_init` and full pin setup belong to the application.

**No floating point.** The programmer does not enable the FPU before calling an
entry point, so the image is built with `USE_FPU=no`.

**Entry points are stateless.** Every entry point re-establishes clock, delay,
board, SPI and flash wake from scratch, and trusts nothing left by an earlier
call. The trace shows `Init` called before every operation anyway. The cost
is a 2 ms flash wake per call.

**Touch only what reaching the flash requires.** The disassembly of both images
references RCC, FLASH, DWT/CoreDebug, GPIOA and the SPI peripheral, and nothing
else. There is no PWR access and no write to `RCC_BDCR`, and the RTC and backup
registers cannot be written without the `PWR_CR1_DBP` bit, which is never set.
`loaderBoardInit()` enables only the ports the flash lines use.

## The part driver

`loader_flash.h` is the whole interface: probe, size, sector size, read, and,
in the RW image only, erase-sector and program.

**Why not the firmware driver.** The firmware's `at25xe.c` sits on the tag's
bus lifecycle -- power and sleep policy, `idlePowerMode`, semaphores,
`stopMilliseconds`, kernel sleeps -- which reaches `ch.h` through
`storage_spi.h`, `core_sync.h` and `phase_probe.h`. More importantly, its `wake`
hook clears block protection with a Write Status Register when the protect bits
are set. That mutates the part, and the bits are themselves evidence. A
loader-specific driver avoids both problems in about 150 lines.

**What is shared.** Opcodes, register bits, JEDEC identity and timing budgets
live in `tags/common/storage/inc/at25xe_commands.h`, which has no dependencies
and is included by both drivers. The budgets encode measured behaviour -- the
sector-erase budget was raised after a 4% failure rate -- so keeping one copy
means a budget raised for the firmware is raised for the loader too. Moving the
constants there left all seven AT25XE firmware targets (PresTag, PresTagRaw,
UIUCTag, CompassTagAT25, CompassTagAT25Breakout, BitTagNG, BitPresTag)
byte-identical in their `.list` disassembly.

**Probe does not write.** It sends Resume from Power-Down (`ABh`), waits
`AT25XE_WAKE_DELAY_MS`, and reads the JEDEC ID. On the AT25XE321D, `ABh` is the
only way out of Ultra-Deep Power-Down, which is where the tag leaves the part:
deasserting chip select is not enough. The 2 ms wait covers the datasheet's
worst-case tRUDPD of 1200 us.

**Success is a read-back, never the busy bit.** A block-protected array
silently ignores Program and Erase: BSY never rises, and a poll sees immediate
"success". So:

- *Erase* works in 4 kB sectors. A sector that already reads blank is skipped.
  Otherwise the driver clears Status Register 1 protection if it is set and
  confirms it cleared, erases within the firmware's measured budget, and
  requires the sector to read back as all `0xFF`.
- *Program* splits at 256-byte page boundaries, programs each page within the
  page-program budget, then reads the whole range back and compares it. NOR
  programming only clears bits, so programming over unerased data fails the
  comparison instead of leaving a silent mix.

Blank-checks and comparisons stream over SPI without a buffer, so they cost no
SRAM against the programmer's transfer buffer.

**Why 4 kB sectors and not block or chip erase.** The 4 kB sector erase is the
command whose budget the firmware has measured. The datasheet gives a 64 kB
block erase of up to 2250 ms, which is no faster per byte, and a chip erase of
65-75 s typical *with no maximum*, which is not a budget at all. Skipping blank
sectors is the real speed-up, and on a partly filled tag it is a large one.

## Rescue erase

Erasing external flash is a rescue operation, always done together with erasing
internal flash. The two must not be left disagreeing. PresTag's own erase
(`dirtyExternalSectors()` in `families/PresTag/src/datalog.c`) erases only the
sectors that its internal page headers cover:

| State | Effect | Recovery |
| --- | --- | --- |
| Internal index kept, external blank | Download returns blank pages. Visible. | `tag-reset` erases what the index covers, and the tag is consistent again. |
| Internal blank, external still written | The tag's next erase covers zero sectors. New pages are programmed over old data, and **the result is corrupted without any error**. | A full external erase. |

So the order is fixed: erase external flash and blank-check it, then erase
internal flash and reflash, then `tag-reset`. If the procedure is interrupted,
the tag is left in the harmless state. A plain full-chip internal erase
(`-e all`, or the GUI) with no external erase produces the harmful state.

## Performance

At SCK = 8 MHz through an ST-LINK-protocol base: 64 KB reads in 0.68 s, and
4 MB in about 70 s (roughly 60 KB/s). Writing 65 KB takes 2.1 s over blank
sectors and 4.6 s when 17 sectors need a real erase first. The per-call
re-initialisation, including the 2 ms wake, is a measurable share of read time
at CubeProgrammer's chunk size. A driver that controls its own chunking could
initialise once per session.

## The Serve() entry point

`Serve(buffer, size)` is the host library's entry point, beside the ST set.
It initialises clock, board, SPI and the part once. It records the JEDEC ID
and status register as found, then loops on the `loaderService` command block
(`include/loader_service.h`) until the host sends EXIT. Erase and program
commands are answered `LOADER_STATUS_READ_ONLY` in an image built without
`LOADER_ALLOW_WRITE`. Because the service block is in `.bss`, the image now has
a non-empty `.bss`, so ld would warn about an RWX segment; `make.mk` passes
`--no-warn-rwx-segments`. The protocol and the host side are in
[SWD Capture and Recovery Library](../../../host/libraries/tagcore/design/swd-recovery.md#loader-protocol-a-service-wrapper-around-the-st-entry-points).

## Open issues

**A loader session changes the next boot's recorded reset cause.** *Settled
2026-10-01: CubeProgrammer's exit, not the loader.* A `tag-xflash` session,
which downloads and runs the same loader and ends in a plain reset, left every
backup register unchanged. A CubeProgrammer read of the same tag changed
`resetCause` from 2 to 1 and rounded `external_blocks` up to a page, because
CubeProgrammer lets the firmware boot as it leaves. See
`host/libraries/tagcore/design/swd-recovery.md`, step 3.
The original note follows. After any
CubeProgrammer session that uses a loader, read-only or read-write,
`RTC_BKP2R` (`pState->resetCause`) reads 1 (`resetStandby`) where it
otherwise reads 2 (`resetShutdown`). Eight plain connections and a plain
software reset left it at 2. The RTC time and every other backup register were
unaffected. The loader cannot have written the register (see the runtime rules
above). The value is written by the firmware at its next boot, from the reset
flags and the shutdown marker, so the likely cause is how CubeProgrammer leaves
the core after a loader session. This is not confirmed. It matters because
`resetCause` steers boot recovery (`main.c` treats standby and shutdown wakes
differently). A host tool that controls the exit sequence can test it.

**`tag_capture_state.py` does not work on STM32L432.** Its SRAM step requests
256 KB (the U375's size) and fails, and its backup-register step also fails.
Both come from U375 constants in the script. For the backup registers it
writes the U375's `RCC_APB1ENR1` at `0x40030C9C` (`RTCAPBEN` is bit 30 there);
on the L432 that address maps to nothing, and the register is at `0x40021058`
with `RTCAPBEN` at bit 10.
The internal-flash regions capture correctly. Until it is fixed, read the
backup registers by hand: under reset, enable `RCC_APB1ENR1_RTCAPBEN`, then
read `0x40002850`, 128 bytes.

**The ST entry points do not refresh the watchdog.** `Serve()` does
(2026-10-01): it writes `IWDG_KR = 0xAAAA` while it waits for commands. The
ST entry points still do not. A tag whose option bytes select
the hardware watchdog (`FLASH_OPTR.IWDG_SW` = 0) has it running from reset, and
the debug freeze covers only a halted core. A long run of loader code -- a mass
erase above all -- would then be cut off by a reset. The bench PresTag uses the
software watchdog, so it is unaffected. The fix is a `IWDG_KR = 0xAAAA` write in
the poll loops, which has no effect when the watchdog is not running; see
[SWD Capture and Recovery Library](../../../host/libraries/tagcore/design/swd-recovery.md#mcu-reference).

**`MassErase` is untested.** Whether `-e all` with a loader loaded also erases
internal flash has not been established, and was not tried on a tag with
firmware worth keeping. Sector erase is tested, through the programmer's
erase-before-write.

**CubeProgrammer is a poor driver for this.** Its loader contract is
undocumented, it rejects a malformed loader silently, its sector-number erase is
ambiguous between internal and external memory, and it knows nothing of the
capture-first and external-before-internal ordering. A host library on
`tagcore` is the planned replacement; see
[SWD Capture and Recovery Library](../../../host/libraries/tagcore/design/swd-recovery.md). The base firmware already implements the ST-LINK
core-register, run and debug-register commands the calling convention needs,
and `LinkAdapt` already provides attach-under-reset and memory access.
