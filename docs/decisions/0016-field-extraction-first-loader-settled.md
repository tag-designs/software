---
type: decision
status: accepted
summary: The first external-flash loader runs polled on ChibiOS register headers and PAL with interrupts off, uses its own non-writing part driver that shares only a command-set header with the firmware, sets HSI16 by hand, and keeps StorageInfo at address 0.
---

# 0016. Field extraction: the first external-flash loader settles the loader runtime

Date: 2026-09-30

Recorded when `AT25XE_PresTagv3` was built, the first SRAM-resident loader for reading a returned tag's external flash without touching internal flash. Cut verbatim from [Field Data Extraction](../../embedded/tags/design/proposals/field-data-extraction.md), "What the first loader settled".

## Context

Building `AT25XE_PresTagv3` answered the questions this section used to leave
open, and overturned three of its expectations. The detail is in
[Loader Runtime Design](../../embedded/loaders/design/loader-runtime.md).

## Decision

**The HAL route works, without interrupts.** The loader uses ChibiOS's register
headers, the committed `board.h` and PAL, with the os-less OSAL and
`osalconf.h` in place of `chconf.h`. It needs no vector table and no ISR path,
because the PresTag family already runs SPI as polled register access
(`HAL_USE_SPI FALSE`). ST's own loaders disable interrupts in every entry
point, and so does this one.

**Moving the drivers to the OSAL would not have made them loader-ready.** This
document expected the 18 `chThdSleep*` call sites to be the only obstacle. But
the os-less `osalThreadSleep` waits on a SysTick-driven virtual timer, which
never advances with interrupts off, and `osalSysPolledDelayX` is an empty stub.
The dependency web is also wider than listed: `storage_spi.h` reaches `ch.h`
and writes `idlePowerMode`, `spi_bus.c` waits on a semaphore, and `at25xe.c`
calls `stopMilliseconds`.

**The firmware driver is unsuitable for forensic reads in any case.** Its
`wake` hook clears block protection with a Write Status Register whenever the
protect bits are set, which mutates the part, and the bits are evidence. The
loader has its own small driver whose probe never writes, and it shares the
command set header with the firmware. Moving the AT25XE constants there left
all seven AT25XE firmware targets byte-identical.

**The calling convention**, from `-vb 3` traces: the programmer leaves
interrupts as the tag had them (the loader disables them), sets a `BKPT` return trap at
`0x20000000`, MSP just past the image (about 1 KB of stack), the transfer
buffer after that, and `Init` before every operation. Nothing survives between
calls that the loader relies on. The SRAM cost is about 1.5 KB for the
read-only image and 2.3 KB for the RW image.

**`StorageInfo` must sit at address 0** in its own segment. A loader with its
descriptor in SRAM is ignored without any message, and reads then silently
bypass it. This was not in any of the material this document drew on.

**The clock is HSI16, set by hand.** ChibiOS's `stm32_clock_init()` can reset
the whole backup domain -- RTC and `pState` -- and must never be called from a
loader.

**Bring-up from cold** on PresTagv3 needs no power sequencing: the flash is on
the main supply.

**`board.h` is committed** for PresTagv3, so the loader builds from a checkout
without `fmpp`, as the reproducibility work intended.

**Provenance** is handled as proposed: loaders are built by
`distributed_firmware`, installed beside the firmware, carry a build manifest,
and put the variant and commit in the device name CubeProgrammer displays.

## Alternatives considered

ST's External Memory Manager was considered and does not fit. Its custom-driver
configuration is XSPI vocabulary -- dummy cycles, single/dual/quad modes, DQS,
instruction and address widths -- while every external flash in this tree hangs
off SPI1 (`TAG_SPI1_DEVICE_DEFAULTS` in
[`spi_bus.h`](../../embedded/tags/common/core/inc/spi_bus.h)). Its parameter list
is still a useful checklist of what a loader's `Init` must establish -- JEDEC
ID, capacity, reset method, read opcode, dummy cycles, chip-select timing --
and if a future board puts flash on OCTOSPI it becomes worth revisiting, since
a generated loader would beat a written one. Note that ST's generated loaders
are read/write by design, so the read-only stubbing above would have to be
applied deliberately.

## Consequences

**STM32CubeProgrammer is a poor driver for this.** Its contract is
undocumented, it rejects a malformed loader silently, and its sector-number
erase is ambiguous between internal and external memory when a loader is
loaded. It also knows nothing of capture-first or external-before-internal
ordering. A host tool on `tagcore`, using the same loader images, is the next
step; see [Where to start](../../embedded/tags/design/proposals/field-data-extraction.md#where-to-start).
