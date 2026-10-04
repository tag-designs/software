---
type: design
status: current
summary: How the tagcore SWD capture and recovery library works - attaching without booting, what a capture holds, the per-MCU rules, the identity record and the Serve() loader protocol.
---

# SWD Capture and Recovery Library

`tagcore/recovery` captures a tag's whole state over SWD **without running its
firmware**, and reads its external flash through an SRAM-resident loader. The
session halts the core at its reset vector before the first instruction runs. It
reads registers, internal flash, optionally SRAM, and then external flash
through the loader that the image's identity record names. The library is C++,
Qt-free, and drives the loaders in `embedded/loaders` through their `Serve()`
entry point, with no dependency on STM32CubeProgrammer.

These tools are built on it:
- `tag-capture`;
- `tag-xflash` (`dump`, `nand`);
- `tag-sramcall`;
- `tag-rebuild`, through `CaptureSource`.

Their usage is in the
[command-line tools README](../../../commandline/README.md#tag-capture), and the
bench procedure is [Capturing a Tag](../../../../docs/bench/capturing-a-tag.md).
Three pieces are not built: the Python binding, identification of images
without an identity record, and rescue erase. They are proposed in
[Identification, Rescue and Python](proposals/recovery-identification-rescue-python.md).
Open work is in [recovery/TODO.md](../recovery/TODO.md).

Background:

- [External Flash Loaders](../../../../embedded/loaders/README.md) and
  [Loader Runtime Design](../../../../embedded/loaders/design/loader-runtime.md):
  the loader images, and their contract as traced on hardware.
- [The SRAM-loader decision](../../../../docs/decisions/0022-field-extraction-sram-loader-not-recovery-firmware.md):
  why external flash is read by an SRAM loader and not by a recovery firmware.
- [Decision 0018](../../../../docs/decisions/0018-offline-rebuild-capture-backed-source.md):
  how a capture is turned back into a SQLite download.
- [SWD Capture Library Bring-up](investigations/2026-09-swd-capture-library-bring-up.md):
  how each layer was checked on bench tags, with the measurements.

## Scope

The library does three things:
- captures everything a returned tag holds into one directory with a manifest;
- reads external flash through the loaders;
- runs an arbitrary routine in SRAM (`SramCall`), which is the layer the
  loaders and the RV3028 probe share.

It does not program internal flash: CubeProgrammer and the `-download` targets
do that. It is not a monitor client (`TagMonitor`), and it does not decode
captured data. `CaptureSource` feeds captures to the existing SQLite writer.

## Session model

A tag has one SWD connection, and that connection is one claimed USB interface
on one base. There is no sharing. A **recovery session** (`SwdSession`) and a
**monitor session** (`TagMonitor`) are alternatives. Each claims the device for
its lifetime, and opening one while the other holds the device fails. The
library reports that failure plainly -- "the base is in use by another session
or application" -- because the underlying libusb claim error does not say so.

Procedures that need both run them **in sequence**: open one session, close it,
then open the next. No session knows about any other; the procedure owns the
order.

**Capture before anything runs the firmware.** A monitor attach resets the tag
and lets its firmware boot. That boot:
- clears the `RCC_CSR` reset flags;
- reclassifies the reset into `pState->resetCause`;
- may rewrite other `pState` words;
- may append a marker to the flash log.

So on a returned tag even `tag-info` destroys evidence. The capture is the
first thing done to a returned tag.

## Attaching without booting the firmware

**Attach.** With NRST asserted, `SwdSession`:
1. enters SWD mode;
2. identifies the MCU from `DBGMCU_IDCODE`, reads the target voltage, and
   records `DHCSR`, `DEMCR` and `DBGMCU_APB1FZR1` as found;
3. writes `DHCSR = DBGKEY | C_DEBUGEN | C_HALT`;
4. writes `DEMCR = VC_CORERESET`;
5. releases NRST.

The core takes the reset vector-catch and halts at the reset handler. The
session confirms `DHCSR.S_HALT` and reads PC. PC must equal the reset vector
read from the second word of flash; the only exception is blank flash. If PC is
wrong, the session closes and refuses to go on, because the capture would not
be of a tag that had not run. It then sets `DBGMCU_APB1FZR1.DBG_IWDG_STOP`, so a
hardware watchdog cannot reset the tag while the core is halted. `Close()`
restores the bit's earlier value.

**The vector-catch bit is shared with the monitor.** On every tag the monitor
uses `DEMCR.VC_CORERESET` as its attachment flag to the firmware: both the L4
and the U3 attach in `tagmonitor.cc` set it, and the firmware reads it as
`MONCONNECTED` (`core_types.h`). If a recovery session left it set, the next boot would
believe a monitor was attached, and the tag would stay awake: a held monitor
reads as a high average. So `Close()` clears `DEMCR` and then ends the session
in one of two declared ways (`SwdExit`):

- `HardwareReset`, the default: pulse NRST with `DHCSR.C_DEBUGEN` clear, so
  the tag boots exactly as it would after a plain connection.
- `LeaveHalted`: the core stays halted, for a follow-on step that needs it.

**`DHCSR.C_MASKINTS` is cleared too, while halted.** `Run()` and `Halt()` set
it so that loader code never takes the tag's interrupts.
- A system reset does not clear it. Only a power-on clears it, or a Standby or
  Shutdown that powers down the core's debug logic.
- It can change only while the core is halted, and only in a write that keeps
  `C_HALT` set. The architecture makes a write that changes it and releases the
  halt UNPREDICTABLE. The L432's Cortex-M4 ignores such a write; the U375's
  Cortex-M33 has not been checked.
- With `C_DEBUGEN` clear the bit is inert. But the next debugger to set
  `C_DEBUGEN`, the monitor included, masks every interrupt, and the firmware
  sits in its idle thread without answering.

`Close()` therefore clears it first: it halts if needed, writes
`DBGKEY | C_DEBUGEN | C_HALT`, and checks the bit. As a second line of defence,
the monitor attach (`TagMonitor::ClearStaleMaskInts()`) clears a stale bit
while it holds the core at the reset vector, and logs that it did. That
recovers a tag left this way by an older build or by another tool.

**The exit is a reset, and the firmware classifies it.** An NRST with valid
retained state and no failure flag is recorded as an external reset. Recovery
treats it as a reattach, so an active run resumes; see
[restart recovery](../../../../embedded/tags/common/core/design/restart-recovery.md#reattach-versus-failure).
Firmware older than `a406eda7` aborts a run that sleeps in Stop 2 instead.
An SWD session ending in `HardwareReset` does not change the next boot's
`resetCause`; a CubeProgrammer loader session does, because CubeProgrammer lets
the firmware boot on exit. Both were measured on a bench PresTag on 2026-09-30
and 2026-10-01; see
[the reset-cause investigation](../../../../embedded/loaders/design/investigations/2026-10-loader-session-reset-cause.md).

**CubeProgrammer's connect-under-reset does not stop the firmware first.** Two
consecutive `STM32_Programmer_CLI -c port=SWD mode=UR` reads of SRAM1 on the
same bench PresTag differed in about 14,900 of 49,152 bytes (recorded
2026-09-30, commit `dd9987de`). So the firmware runs
between reset release and CubeProgrammer's halt. Anything captured through
CubeProgrammer, including by `tag_capture_state.py`, describes a tag that has
started to boot.

## What a capture contains

The regions are read least disturbing first (`statecapture.*`,
`externalcapture.*`):

| # | Region | How | Notes |
| --- | --- | --- | --- |
| 1 | Core and system registers | memory reads | `FLASH_OPTR` (option bytes); the flash ECC registers, read **before** step 2 because step 2 can overwrite them; `RCC_CSR` (reset flags, which accumulate until firmware clears them); `RCC_BDCR`; RTC time and date; all backup registers, after enabling `RTCAPBEN`; the unique ID; the flash-size register; raw blocks of the RCC, FLASH, PWR/TAMP, RTC and DBGMCU registers; OTP and, on the L432, the memory-mapped option bytes. |
| 2 | Internal flash | memory reads | The whole array, page by page: image, persistent region, configuration and NAND-map pages. No code runs. The ECC registers are read again afterwards. |
| 3 | SRAM | memory reads | **Opt-in** (`--sram`). What SRAM can hold depends on the MCU. **STM32L432** tags idle in Shutdown or Standby; neither keeps SRAM here (the firmware clears `PWR_CR3.RRS`), so an idle L432 tag's SRAM rarely holds anything. **STM32U375** tags idle in Stop 3, and the firmware leaves `PWR_CR2`'s SRAM power-down bits at their reset value, so SRAM should be kept through the sleep; but every Stop 3 wake ends in `NVIC_SystemReset()` and a fresh boot, so ordinary `.data` and `.bss` describe the last boot, not the run. Retention of the scratchpad through Stop 3 and that reset is not yet verified on hardware. On either MCU the attach's NRST then erases SRAM if the option bytes say so (rule below). The state that matters is in the backup registers of step 1. SRAM is useful mainly for a tag that stopped while running, and for the U375 scratchpad in the last 8 KB of SRAM2. It must precede step 4, because the loader overwrites the start of SRAM1. When the SRAM erase-on-reset option is set, the manifest marks SRAM untrustworthy: the attach itself erased it. |
| 4 | External flash | loader | NOR: the whole part, raw. SPI NAND: whole pages, data and spare, read both raw and through the part's on-die ECC, with each page's ECC verdict; blank blocks are skipped unless `--external-full` is given. The loader is the one the identity record names, and the part's JEDEC ID is checked against the record. |

Steps 1-3 need nothing but SWD reads, and they work on any tag, including one
whose external flash or loader is unknown.

**Output.** One timestamped directory: one file per region, and a
`manifest.json`. The manifest records:
- the reason given;
- the attach details: halted, PC, reset vector, `DHCSR` and `DEMCR` as found;
- the decoded registers;
- the target voltage;
- each region's address, size, SHA-256 and outcome;
- the identity record;
- the loader used and its SHA-256, with the part's JEDEC ID and status as found;
- how the session ended.

A region that fails is recorded as failed, and the capture continues. A partial
capture of a damaged tag is still the most valuable thing the tool produces.

## MCU reference

The addresses and rules below come from RM0394 Rev 5 (STM32L41x-L46x) and
RM0487 Rev 3 (STM32U3). Peripheral bases are cross-checked against the CMSIS
device headers in the ChibiOS submodule (`stm32l432xx.h`, `stm32u375xx.h`).
U375 addresses are the nonsecure aliases. They live in one table per MCU in
`swdmcu.cc`, not in the procedures.

**No protections are set on tags.** Tags ship at readout-protection level 0,
with no write protection, no PCROP, and TrustZone off. This library assumes
that and does not handle the other levels. The capture still records
`FLASH_OPTR`, so a tag that somehow differs is visible in its manifest. At any
other level, debug reads of flash fail, and lowering the level mass-erases
flash, SRAM and the backup registers. If protections are ever introduced, this
design has to be revisited first. A capture of the bench PresTag on 2026-09-30
read `FLASH_OPTR = 0xFFFFF8AA`: level 0, `SRAM2_RST` = 1 and `IWDG_SW` = 1.

| | STM32L432 | STM32U375 |
| --- | --- | --- |
| Main flash | 256 KB at `0x08000000`, one bank, 128 pages of 2 KB | 1 MB at `0x08000000`, two banks of 512 KB, 4 KB pages; bank 2 at `0x08080000` |
| OTP | 1 KB at `0x1FFF7000` | 512 B at `0x0BFA0000` |
| Option bytes | Memory-mapped at `0x1FFF7800`; live copy in `FLASH_OPTR` at `0x40022020` | **Not memory-mapped**; `FLASH_OPTR` at `0x40022040` only |
| Unique ID (96 bits) | `0x1FFF7590` | `0x0BFA0700` |
| Flash size (KB) | `0x1FFF75E0` | `0x0BFA07A0` |
| SRAM1 | 48 KB at `0x20000000` | 192 KB at `0x20000000` |
| SRAM2 | 16 KB at `0x10000000`, also at `0x2000C000` | 64 KB at `0x20030000`; scratchpad in its last 8 KB (`0x2003E000`) |
| Backup registers | 32 x `RTC_BKPxR` at `0x40002850` | 32 x `TAMP_BKPxR` at `0x40007D00` |
| Backup register access | `RCC_APB1ENR1.RTCAPBEN` (bit 10, `0x40021058`) | `RCC_APB1ENR1.RTCAPBEN` (bit 30, `0x40030C9C`) |
| RTC | `0x40002800` | `0x40007800` |
| Reset flags `RCC_CSR` | `0x40021094` | `0x40030D14` |
| `RCC_BDCR` | `0x40021090` | `0x40030D10` |
| Flash ECC status | `FLASH_ECCR` at `0x40022018` | `FLASH_ECCCR` `0x40022030`, `FLASH_ECCDR` `0x40022034` |
| IWDG freeze on halt | `DBGMCU_APB1FZR1` bit 12 (`0xE0042008`) | `DBGMCU_APB1FZR1` bit 12 (`0xE0044008`) |

Four rules follow from the manuals, and each changes what the capture does.

**Connecting under reset can erase SRAM before anything is read.** On both parts
an option bit decides whether a system reset erases SRAM: `SRAM2_RST` (bit 25)
on both, and on the U3 also bit 15 for the other SRAMs. The U3 manual gives two
variants of `FLASH_OPTR` in which bit 15 is `SRAM_RST` or `SRAM1_RST`. NRST is a
system reset, and this rig cannot attach without it. So on a tag whose option
clears SRAM on reset, the SRAM capture is of erased memory. The capture reads
`FLASH_OPTR`, records that the SRAM contents cannot be trusted, and still saves
them. The fix is a provisioning policy -- set the "not erased" option on every
tag -- not something the tool can do after the fact. The bench PresTag has
`SRAM2_RST` = 1 (2026-09-30 capture). The U375's Stop 3 wake is itself a system
reset (`NVIC_SystemReset()`), so the same option decides whether SRAM survives
the firmware's own wake as well as the attach. Separately, SRAM1 is lost in
Standby on both parts, and SRAM2 survives Standby only where the firmware
enables retention; this applies to the L432 tags, whose firmware clears
`PWR_CR3.RRS`, and not to the U375 tags, which do not use Standby.

**The ECC registers are evidence, and a capture can overwrite them.** They hold
the address of the last double-word that failed ECC. That may be the tag's own
fault. They do not update again until the flags are cleared, and a flash read
of an erased double-word can report a corrected error on the L4. So read the
ECC registers **before** reading internal flash, record them, and read them
again afterwards. A double error raises an NMI in the tag. The internal-flash
read goes page by page, so one failing page is recorded without losing the
rest.

**The watchdog may already be running.** `FLASH_OPTR.IWDG_SW` (bit 16) = 0
selects the hardware watchdog, which starts at reset and cannot be stopped.
While the core is halted, `DBGMCU_APB1FZR1.DBG_IWDG_STOP` freezes it, so the
session sets that bit on attach. That does not cover code running on the tag.
A loader or `Serve()` session on such a tag must refresh the watchdog
(`IWDG_KR = 0xAAAA`) inside its poll loops, or a long erase is cut off by a
reset. The bench PresTag has `IWDG_SW` = 1 (software watchdog; 2026-09-30
capture).

**Backup registers read as zeros until their bus clock is on.** Connecting under
reset resets RCC. Set `RTCAPBEN` before reading them, as `tag_capture_state.py`
does on the U375. The register differs between the two parts, which is one
reason that script fails on the L432.

## Identifying the tag, and choosing a loader

A tag that cannot talk cannot say what it is, so identification works from what
the capture has already read. Every image built since the identity record
carries one directly after the interrupt vectors. The session already knows the
MCU from `DBGMCU_IDCODE`, and the vector table has a fixed size per MCU, so the
record sits at a known address: `0x080001A0` on the STM32L432 and `0x08000240`
on the STM32U375. It is read in one step, with no scan (`identityrecord.*`). It
names the loader and the decoder, and it carries the layout facts a decoder
needs. The format and the reasons for it are in
[decision 0021](../../../../docs/decisions/0021-offline-rebuild-tag-identity-record.md);
`embedded/tools/decode_tag_identity.py` prints one.

`--loader` overrides the record, and it is the only way to capture external
flash from an image without a record. `tag-xflash` always takes its loader by
argument. Whatever the source, the part's JEDEC ID is checked. The record stores
it as `manufacturer << 16 | device1` (for example `0x1F0047`), because the
firmware knows only those two bytes, while the loader reports all three
(`0x1F4708`). Compare those two bytes, not the whole word.

## Loader protocol: a service wrapper around the ST entry points

The loaders keep the STM32CubeProgrammer entry points (`Init`, `Read`, `Write`,
`SectorErase`, `MassErase`), so CubeProgrammer remains a working fallback. They
also export `Serve(buffer, size)`, which the library uses.

Calling each ST entry point separately costs a register setup and a run/halt
round trip per call. Each call also re-initialises clock, SPI and flash wake,
including a 2 ms wake delay, and it can only return 0 or 1. `Serve()`
initialises once. It then loops on the `LoaderServiceBlock` command block,
declared in `include/loader_service.h` and shared by loader and host:
- `magic`, `version`, then `seq` and `ack`;
- `cmd`, which is one of `PROBE`, `READ`, `ERASE_SECTOR`, `PROGRAM`, `EXIT`
  or `READ_PAGE` (version 2, for NAND: one page, raw or through on-die ECC);
- `offset`, `length` and `status`;
- `detail[]`;
- `progress`.

- **The host finds the block by symbol and chooses the buffer.** It looks up
  `loaderService` in the loader ELF's symbol table, as it does `Serve`. It
  passes the transfer buffer to `Serve()` as an argument, so the host decides
  where the buffer goes.
- **The host zeroes the block before starting `Serve()`.** The block is in
  `.bss`, which is not downloaded, and SRAM survives a reset. Without the
  zeroing, a magic left by an earlier session would read as ready.
- **One buffer.** SWD reads through a base run at about 101 KB/s, a tenth of
  what the flash's 8 MHz SPI delivers. The loader fills a buffer far faster
  than the host can fetch it, so double buffering would buy nothing. A
  `Serve()` read runs at about 78 KB/s, close to that bound. The measurements
  are in the [bring-up investigation](investigations/2026-09-swd-capture-library-bring-up.md#implementation-sequence).
- **Detailed results.** `detail[]` carries what the ST convention throws away:
  - the part's identity;
  - the status register as found (the protect bits are evidence);
  - which offset failed;
  - for NAND, each page's ECC status.
- **Commands are bounded.** Every command completes within the loader's timing
  budgets. The host also times out on `ack`, and halts the core if `ack` never
  arrives.
- **The loader refreshes the tag's watchdog.** When the hardware watchdog is
  selected (`FLASH_OPTR.IWDG_SW` = 0), it runs while the loader runs, and the
  debug freeze covers only a halted core. `Serve()` therefore writes
  `IWDG_KR = 0xAAAA` in its poll loop. The write has no effect when the
  watchdog is not running.
- **Read-only images stay read-only.** In an image built without
  `LOADER_ALLOW_WRITE`, `Serve()` answers `ERASE_SECTOR` and `PROGRAM` with
  `LOADER_STATUS_READ_ONLY`. The code is absent, as it is for the ST entry
  points.

The library also keeps a generic path, `SramCall`, that calls a named
ST-style entry point with the traced convention:
- a `BKPT` return trap at the start of SRAM1, with LR set to it;
- MSP 1 KB past the image;
- arguments in R0-R3, and the result in R0;
- interrupts masked with `DHCSR.C_MASKINTS`.

Any `.stldr` still works through it, ST's own included (`tag-xflash dump --st`),
and so does an SRAM probe such as `RV3028_PresTagv3` (`tag-sramcall`).

## Library architecture

Everything is C++ in `tagcore`, and Qt-free. The layers, lowest first:

| Layer | Files | Responsibility |
| --- | --- | --- |
| `LinkAdapt` | `linkadapt.*` | USB claim, ST-LINK protocol, memory and debug-register access, NRST |
| `SwdSession` | `recovery/swdsession.*`, `recovery/swdmcu.*` | Owns the connection for a recovery session: attach-without-boot, declared exit, core control (halt, run, wait-for-halt, core registers through `DCRSR`/`DCRDR`), and register and memory reads of named regions per MCU |
| `TargetImage` | `recovery/targetimage.*` | Minimal ELF32 reader: loadable segments and symbols, with no external dependency |
| `SramCall` | `recovery/sramcall.*` | Download a `TargetImage` into SRAM and call a symbol, with the traced convention and a timeout |
| `ExternalFlash` | `recovery/externalflash.*` | A loader session through `Serve()`: probe, read, read page, erase, program |
| `IdentityRecord` | `recovery/identityrecord.*` | Parse the identity record from captured internal flash |
| Capture | `recovery/statecapture.*`, `recovery/externalcapture.*` | The capture procedure: ordering, output directory, manifest |
| `CaptureSource` | `recovery/capturesource.*` | Rebuild the monitor replies from a capture, through a per-family decoder chosen by the record's `decoder` name |

Halting goes through `DHCSR` writes over `LinkAdapt`'s debug-register access,
so the base's `FORCEDEBUG` handler is not used. MCU knowledge lives in one
table per MCU in `swdmcu.cc`, not in the procedures: the flash base and page
size, the SRAM ranges, register addresses (including the flash-size register,
from which the capture sizes internal flash at run time), the backup-register enable, the IWDG freeze bit and the
identity-record address.
