---
type: design
status: current
summary: The external loader contract as traced on hardware and the runtime rules it imposes -- no startup code, interrupts or OSAL sleeps, rescue erase, Serve() -- for the STM32L432 NOR loaders and the STM32U375 SPI-NAND loader.
---

# Loader Runtime Design

This document records the contract a loader runs under, the rules that follow
from it, and the reasons for each, so that the next loader starts from what is
known rather than rediscovering it. The contract and rules were established on
`AT25XE_PresTagv3` (STM32L432, AT25XE SPI NOR) and validated on hardware; the
STM32U375 SPI-NAND loader, `GD5F2GM7RE_IMUTagNandv2`, follows the same rules
with the differences in
[STM32U375 and SPI NAND](#stm32u375-and-spi-nand). The orientation and the
add-a-loader checklist are in the [loaders README](../README.md); the case for
loaders at all is
[decision 0022](../../../docs/decisions/0022-field-extraction-sram-loader-not-recovery-firmware.md).
The design decisions behind the first loader are
[decision 0016](../../../docs/decisions/0016-field-extraction-first-loader-settled.md).

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
is at most four register writes (`loader_clock.c`), touching only `RCC_CR`,
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

These figures were recorded with the first loader, `AT25XE_PresTagv3`, when
this document was written (commit `326afc32`); no results log holds them and
they have not been re-measured since.

At SCK = 8 MHz through an ST-LINK-protocol base: 64 KB reads in 0.68 s, and
4 MB in about 70 s (roughly 60 KB/s). Writing 65 KB takes 2.1 s over blank
sectors and 4.6 s when 17 sectors need a real erase first. The per-call
re-initialisation, including the 2 ms wake, is a measurable share of read time
at CubeProgrammer's chunk size. A driver that controls its own chunking could
initialise once per session.

## STM32U375 and SPI NAND

`GD5F2GM7RE_IMUTagNandv2` reads the IMUTagNandBmp581's GigaDevice GD5F2GM7RE
(256 MiB SPI NAND: 2048 data plus 128 spare bytes per page, 64 pages per block,
2048 blocks). Everything in the runtime rules above applies; what differs is
below. Why each choice was made is in the
[decision record](../../../docs/decisions/0023-loader-u375-nand-loader-reads-without-reset.md).

**Build.** `common/make-u375.mk` mirrors `make.mk` with the U3 startup and
platform makefiles, `cfg/stm32u3/` (PAL-only `halconf.h`, `mcuconf.h` with
`STM32_NO_INIT`), `STM32U375-loader.ld` (`StorageInfo` at 0, the image in SRAM1
from `0x20000004`), `MCU = cortex-m33` and `USE_FPU = no`. The first 64 bytes of
SRAM1 are the firmware's monitor mailbox, which the loader overwrites while the
core is halted.

**Clock.** `loader_clock_u3.c` pins MSIS at MSIRC1/2 = 12 MHz through
`RCC_ICSCR1` (MSIRGSEL = 1): a value the part may already be running at, within
voltage range 2 at the reset's 1 wait state. PWR, the booster, `FLASH_ACR` and
the backup domain are not touched.

**SPI.** `loader_spi_u3.c` is a bounded polled master for the U3's `SPI_TXDR`
style peripheral, following `tags/common/core/src/spi_bus_polled.inc`.

**Power and the shared bus.** The NAND is powered from `FLASH_PWR` (PA8), which
the attach's reset leaves undriven. The loader drives it high, waits, and sends
`ABh` (release from deep power-down) before anything else; without that the
NAND runs briefly on residual charge and then drops off the bus mid-read. The
LSM6DSV16X shares SCK, MISO and MOSI and is always powered, so its chip select
(PB1) is driven high before any SPI traffic. No other sensor pin is touched.
See `boards/IMUTagNandv2/standby-pins.md`.

**The part is read, not changed.** `common/src/gd5f_loader.c` never sends
Reset (`FFh`) and never writes the block-lock register `A0h`; it records `A0h`,
`B0h`, `C0h` and `F0h` as found. Its one write is `B0h` ECC_EN, through `gd5fSetEcc()`,
which changes that bit alone, holds the reserved bits low, and refuses if
OTP_EN or OTP_PRT was found set (OTP_PRT is non-volatile). `Serve()` restores
`B0h` as found before it returns. There is no program or erase code in the
image, and it is built read-only only. Opcodes and timing are shared with the
firmware driver through `tags/common/storage/inc/gd5f_commands.h`.

**Page reads.** `loaderFlashReadPage()` returns 2176 bytes, raw or through ECC,
with `C0h` and `F0h` read after the page. Every page read is followed by a
status read; an all-ones reply means the part was not driving MISO, and the page
is retried rather than returned.

**Paged service.** `include/loader_service.h` version 2 adds
`LOADER_CMD_READ_PAGE` and widens `detail[]` to carry the page status and the
registers as found. All loaders are version 2, and the host accepts 1 or 2.
Choosing which pages to read is the host's job: `tag-xflash nand` and
`tag-capture` read page 0 of each block raw, skip the block if that page is
blank, and otherwise read every page of the block raw and through ECC; see
[SWD Capture and Recovery Library](../../../host/libraries/tagcore/design/swd-recovery.md).
On the bench, a scan of the whole part with one used block took 74 s.

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

## Driving a loader: CubeProgrammer and the host library

CubeProgrammer can drive a loader, but it is a poor driver for this work: its
loader contract is undocumented, it rejects a malformed loader silently, its
sector-number erase is ambiguous between internal and external memory, it knows
nothing of the capture-first and external-before-internal ordering, and it lets
the firmware boot as it leaves, which changes the next boot's recorded
`resetCause` ([investigation](investigations/2026-10-loader-session-reset-cause.md)).
The host library in `tagcore` (`host/libraries/tagcore/recovery/`, used by
`tag-xflash` and `tag-capture`) drives the same images through `Serve()` and
controls the exit; see
[SWD Capture and Recovery Library](../../../host/libraries/tagcore/design/swd-recovery.md).

## Open issues

Open issues are in the [loaders worklist](../TODO.md).
