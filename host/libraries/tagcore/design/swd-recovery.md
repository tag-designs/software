# SWD Capture and Recovery Library

## Status

Partly implemented. Steps 0-2 of the
[implementation sequence](#implementation-sequence) are built and were run on a
PresTag (STM32L432) on 2026-09-30: `tagcore/recovery/` and the `tag-capture`
tool capture registers, option bytes, OTP and internal flash, halted before the
firmware runs.

Steps 3 and 4 are built (2026-10-01). `TargetImage`, `SramCall` and `tag-xflash dump`
call a loader's `Init` and `Read` from the host and stream the external flash
to a file; see step 3 for what has been checked. Step 9, the identity record,
shipped in the firmware ahead of steps 4-8 (`next-release-todo.md` B1). It was
checked on a bench PresTag with `tag-capture` and
`embedded/tools/decode_tag_identity.py`. Everything else here is still a
proposal.

**Resume here.** Step 3's checks passed, and step 4's read path is built and
checked (see each step). The next steps are:
- step 4's remaining checks, which need an erase path through `Serve()`;
- step 5, external flash in `tag-capture`;
- then the offline SQLite shim (`next-release-todo.md` D1) on PresTag. Capture
  and dump a tag, rebuild its SQLite file, and compare it with a normal
  `tag-dwnld` of the same tag.

It defines a Qt-free, Python-usable
library in `tagcore` for capturing a tag's complete state over SWD and for
reading, and in rescue erasing, its external flash through an SRAM-resident
loader. It also sets out the order in which to build it. It replaces
STM32CubeProgrammer as the driver of the loaders in `embedded/loaders`, and
`embedded/tools/tag_capture_state.py` as the capture tool.

Background:

- [Field Data Extraction](../../../../design/field-data-extraction.md): why
  extraction must not overwrite internal flash, and the session superblock
  proposal that the identity record below should be designed with.
- [External Flash Loaders](../../../../embedded/loaders/README.md) and
  [Loader Runtime Design](../../../../embedded/loaders/design/loader-runtime.md):
  the loader images, their contract as traced on hardware, and the open issues
  this library is expected to settle.
- [Python Interface Design](python-interface.md): the binding conventions this
  library follows.

## Goals

- One operation that captures everything a returned tag holds -- registers,
  internal flash, SRAM and external flash -- into one directory with a
  manifest, without running the tag's firmware first.
- Read external flash through the loaders, and erase it in rescue, with the
  ordering rules enforced by the library rather than by documentation.
- Identify which firmware a tag carries, and so which loader it needs, from the
  capture itself when possible.
- A layered C++ API whose lower layers are useful on their own, such as running
  an arbitrary routine in SRAM, and a Python binding over all of it.
- Work through the existing bases, which implement the ST-LINK protocol, with
  no dependency on STM32CubeProgrammer.

## Non-goals

- Programming internal flash. CubeProgrammer and the existing `-download`
  targets do that, and the rescue procedure calls out to them.
- A monitor-protocol client. `TagMonitor` stays as it is.
- Decoding the captured data. The capture feeds the existing decoders; it does
  not replace them.
- Concurrent sessions. See below.

## Session model

A tag has one SWD connection, and that connection is one claimed USB interface
on one base. There is no sharing. A **recovery session** and a **monitor
session** (`TagMonitor`) are alternatives: each claims the device for its
lifetime, and opening one while the other holds the device fails. The library
reports that failure plainly -- "the base is in use by another session or
application" -- because the underlying libusb claim error does not say it.

Procedures that need both run them **in sequence**: open one session, close it,
open the next. The rescue procedure, for example, is a recovery session, then an
internal-flash programming step, then a monitor session for `tag-reset`. No
session knows about any other; the procedure layer owns the order.

**Capture before anything runs the firmware.** A monitor attach resets the tag
and lets its firmware boot. That boot clears the `RCC_CSR` reset flags,
reclassifies the reset into `pState->resetCause`, may rewrite other `pState`
words, and may append a marker to the flash log. So on a returned tag even
`tag-info` destroys evidence. The capture is the first thing done to a returned
tag, and the library and its tools present it that way.

## Attaching without booting the firmware

A recovery session must stop the core **before the first instruction of the
firmware runs**, and leave it in a defined state when it ends.

**Attach.** With NRST asserted (`STLINK_JTAG_DRIVE_NRST`, already used by
`LinkAdapt::Attach`), enter SWD mode, set `DEMCR.VC_CORERESET` and write
`DHCSR = DBGKEY | C_DEBUGEN | C_HALT`, then release NRST. The core takes the
reset vector-catch and halts at the reset handler. The session confirms
`DHCSR.S_HALT` and reads PC. PC must equal the reset vector read from address
`0x08000004`; if it does not, the session refuses to go on, because the capture
would not be of a tag that had not run. It then sets
`DBGMCU_APB1FZR1.DBG_IWDG_STOP`, so a hardware watchdog cannot reset the tag
while the core is halted (see [MCU reference](#mcu-reference)).

**The vector-catch bit is shared with the monitor.** On STM32L4 tags the monitor
uses `DEMCR.VC_CORERESET` as its attachment flag to the firmware
(`tagmonitor.cc`). A recovery session that leaves it set would make the next
boot believe a monitor is attached, and the tag would then stay awake. That is
exactly the "held monitor reads as a high average" failure in AGENTS.md. So:

**Detach.** Clear `DEMCR` (vector catch and monitor bits), then end the session
in one of two declared ways:

- `leave_halted`: the core stays halted, for a follow-on step that needs it.
- `hardware_reset`: pulse NRST with `DHCSR.C_DEBUGEN` clear, so the tag boots
  exactly as it would after a plain connection.

The default is `hardware_reset`. This is also the experiment for the open
`resetCause` issue: CubeProgrammer loader sessions leave `resetCause` =
`resetStandby` where a plain connection leaves `resetShutdown`, and a session
whose exit is known lets that be tested directly. Measured on 2026-09-30: after
a session ending in `hardware_reset`, the next capture read `resetCause` =
`resetShutdown`, as after a plain connection. So an SWD session as such does not
cause the change; what remains is something particular to CubeProgrammer's
loader sessions.

**CubeProgrammer's connect-under-reset does not stop the firmware first.** Two
consecutive `STM32_Programmer_CLI -c port=SWD mode=UR` reads of SRAM1 on the
same PresTag differed in about 14,900 of 49,152 bytes, so the firmware runs
between reset release and CubeProgrammer's halt. Anything captured through
CubeProgrammer -- including by `tag_capture_state.py` -- describes a tag that
has started to boot. The attach above is the first that does not.

## What a capture contains

In the order read, least disturbing first:

| # | Region | How | Notes |
| --- | --- | --- | --- |
| 1 | Core and system registers | memory reads | `FLASH_OPTR` (option bytes), the flash ECC registers (**before** step 2, which can overwrite them), `RCC_CSR` (reset flags, which accumulate until firmware clears them), `RCC_BDCR`, PWR status, RTC time/date/status, all backup registers after enabling `RTCAPBEN`, the unique ID, the flash-size register, OTP, and the core registers of the halted core. |
| 2 | Internal flash | memory reads | The whole array, page by page: image, persistent region, configuration and NAND-map pages. No code runs. The ECC registers are read again afterwards. |
| 3 | SRAM | memory reads | **Opt-in.** Tags spend their idle time in Shutdown or Standby, which do not keep SRAM, so a returned tag's SRAM rarely holds anything; the state that matters is in the backup registers of step 1. Useful mainly for a tag that stopped while running, and for the U375 scratchpad in the last 8 KB of SRAM2. Must precede step 4: the loader overwrites the start of SRAM1. Recorded as untrustworthy when the SRAM erase-on-reset option is set, because the attach itself erased it. |
| 4 | External flash | loader | Raw, whole part. For NAND, raw pages including spare area. |

Steps 1-3 need nothing but SWD reads and work on any tag, including one whose
external flash or loader is unknown. Step 4 needs the loader, chosen as below.

**Output.** One timestamped directory, as `tag_capture_state.py` produces now:
one file per region, and a `manifest.json` recording:

- the tool and library versions;
- the base's serial number;
- the target voltage;
- for each region: its address range, SHA-256, and whether it succeeded;
- the identification result and its basis;
- the loader used, with its image SHA-256 and device name;
- the loader's detail results (JEDEC ID, Status Register 1 as found);
- how the session ended.

A region that fails is recorded as failed and the capture continues. A partial
capture of a damaged tag is still the most valuable thing the tool produces.

## MCU reference

The addresses and rules below come from RM0394 Rev 5 (STM32L41x-L46x) and
RM0487 Rev 3 (STM32U3). Peripheral bases are cross-checked against the CMSIS
device headers in the ChibiOS submodule (`stm32l432xx.h`, `stm32u375xx.h`).
U375 addresses are the nonsecure aliases. They belong in one table per MCU in
`SwdSession`, not in the procedures.

**No protections are set on tags.** Tags ship at readout-protection level 0,
with no write protection, no PCROP, and TrustZone off. This library assumes
that and does not handle the other levels. The capture still records
`FLASH_OPTR`, so a tag that somehow differs is visible in its manifest. At any
other level, debug reads of flash fail, and lowering the level mass-erases
flash, SRAM and the backup registers. If protections are ever introduced, this
design has to be revisited first. The bench PresTag read
`FLASH_OPTR = 0xFFFFF8AA` on 2026-09-30: level 0.

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
`SRAM2_RST` = 1. Separately, SRAM1 is lost in Standby on both parts, and
SRAM2 survives Standby only where the firmware enables retention.

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
reset. The bench PresTag has `IWDG_SW` = 1 (software watchdog).

**Backup registers read as zeros until their bus clock is on.** Connecting under
reset resets RCC. Set `RTCAPBEN` before reading them, as `tag_capture_state.py`
does on the U375. The register differs between the two parts, which is one
reason that script fails on the L432.

## Identifying the tag, and choosing a loader

A tag that cannot talk cannot say what it is, so identification works from what
the capture has already read. Firmware built since the identity record
(`embedded/tags/design/next-release-todo.md` B1) carries it directly after the
vectors, and it names the loader and decoder outright. Read it first. The
three sources below remain for older images, such as the fw-v0.0.3 tags
already deployed, in order of confidence:

1. **Image hash.** Step 2 has the image. Each build manifest records the `.bin`
   SHA-256 and length, so hashing that many bytes of internal flash and looking
   the result up in a catalog of manifests identifies a released image exactly.
   This needs no firmware change and covers every image built since build
   manifests were introduced.
2. **Static strings.** For images not in the catalog -- development builds,
   dirty trees -- scan the image for the strings `monitor.c` carries in
   `InfoStrings`: board name, source path (`/embedded/tags/PresTag`), git hash
   and repository. Every image built with the common monitor has them, but they
   sit at no fixed place, so this is a heuristic, and the manifest records it as
   one.
3. **An explicit argument.** `board=` or `loader=` from the caller. This is the
   only method at first, and it always overrides the other two, with any
   disagreement recorded.

Whatever the source, the loader's `Init` confirms the JEDEC ID of the part it
drives, and the loader refuses to proceed on a mismatch.

**The catalog.** A JSON file generated at build time and installed beside the
firmware and loaders. For each distributed tag target it records the target
name, the image SHA-256 and length, the board, and the loader. The board comes
from the tag's `project.mk`, which the build already reads to derive the
distributed board set. For the mapping from board to loader, `add_embedded_loader`
gains a `BOARD` argument; today the board is implicit in `LOADER_BOARD_INC`.

**The tag identity record** (in firmware since B1; this was the proposal). A const, versioned record placed
by the tag linker script directly after the interrupt vectors. Since the
session already knows the MCU from `DBGMCU_IDCODE`, and the vector table has a
fixed size per MCU, the record sits at a known address: `0x080001A0` on the
STM32L432, `0x08000240` on the STM32U375. It is read in one step, with no
scan. It carries:
- everything the tag-info call reports;
- the board and hardware revision;
- the external flash part;
- the loader and decoder names;
- the layout facts a decoder needs.

The downloader chooses its loader and decoder from it, so no board argument is
needed. The specification is item 4 of
[Offline Log Reconstruction](../../../../design/offline-log-reconstruction.md#4-a-tag-identity-record-immediately-after-the-interrupt-vectors).

This record should be designed together with the session facts, which go with
the stored configuration (item 5 there). Adding it changes every image's
layout, and on STM32U375 layout alone has moved idle current. So it ships only
as a qualified firmware release (`tag_release_check.py`), ideally bundled with
other firmware work.

## Loader protocol: a service wrapper around the ST entry points

The loaders keep the STM32CubeProgrammer entry points (`Init`, `Read`, `Write`,
`SectorErase`, `MassErase`), so CubeProgrammer remains a working fallback, and
gain one more: `Serve()`. The library uses `Serve()`.

Calling each ST entry point separately costs a register setup and a run/halt
round trip per call. Each call also re-initialises clock, SPI and flash wake,
including a 2 ms wake delay, and can only return 0 or 1. `Serve()` initialises
once and then loops on a command block in SRAM:

```c
typedef struct {
  uint32_t magic;         /* 'LSRV': written by the loader when it is ready */
  uint32_t version;       /* protocol version; the host refuses a mismatch */
  uint32_t seq;           /* host increments to submit a command */
  uint32_t ack;           /* loader copies seq when the command completes */
  uint32_t cmd;           /* PROBE, READ, ERASE_SECTOR, PROGRAM, EXIT */
  uint32_t offset;        /* flash offset */
  uint32_t length;        /* bytes */
  int32_t  status;        /* 0 ok, negative error code */
  uint32_t detail[8];     /* JEDEC ID, SR1 as found, failing offset, ... */
  uint32_t progress;      /* bytes done in the current command */
} LoaderServiceBlock;
```

- **Location by symbol, not by address.** The host reads the addresses of the
  service block and the transfer buffer from the loader ELF's symbol table,
  the same way it finds `Serve`.
- **One buffer.** SWD reads run at about 101 KB/s through a base (step 0), a
  tenth of what the flash's 8 MHz SPI delivers, so the loader fills a buffer
  far faster than the host can fetch it. Double buffering would buy nothing.
- **Detailed results.** `detail[]` carries what the ST convention throws away:
  the part's identity, the status register as found (the protect bits are
  evidence), and which sector failed and why.
- **Commands are bounded.** Every command still completes within the loader's
  timing budgets. The host adds its own watchdog over `ack`, and halts the core
  if `ack` never arrives.
- **The loader refreshes the tag's watchdog.** When the hardware watchdog is
  selected (`FLASH_OPTR.IWDG_SW` = 0), it runs while the loader runs, and the
  debug freeze only covers a halted core. `Serve()` therefore writes
  `IWDG_KR = 0xAAAA` in its poll loops; the write has no effect when the
  watchdog is not running. The ST entry points should do the same, since a
  CubeProgrammer mass erase is also a long run.
- **Read-only images stay read-only.** In an image built without
  `LOADER_ALLOW_WRITE`, `Serve()` answers `ERASE_SECTOR` and `PROGRAM` with an
  error. The code is absent, as it is for the ST entry points.

The library also keeps a generic path that calls a named ST-style entry point
with the traced convention: `BKPT` return trap at the start of SRAM, MSP just
past the image, arguments in R0-R3, result in R0. Any `.stldr`, including
ST's own, then still works, and it was the first thing to build (step 3 below).

## Library architecture

Everything is C++ in `tagcore`, Qt-free, with the pybind11 binding described in
[Python Interface Design](python-interface.md). The layers, lowest first:

| Layer | Responsibility | New or existing |
| --- | --- | --- |
| `LinkAdapt` | USB claim, ST-LINK protocol, memory and debug-register access, NRST | Existing; gains core control: halt, run, wait-for-halt, core-register read/write, vector catch |
| `SwdSession` | Owns the connection for a recovery session. Attach-without-boot, declared detach, register and memory reads of named regions per MCU | New |
| `TargetImage` | Minimal ELF32 reader: loadable segments and symbols. No external dependency | New |
| `SramCall` | Download a `TargetImage` into SRAM and call a symbol, with the traced convention and a timeout | New |
| `ExternalFlash` | A loader session through `Serve()`: probe, read, erase, program, with progress | New |
| `Catalog`, `Identify` | Image-hash lookup, string scan, identity record | New |
| `Capture`, `Rescue` | The procedures: ordering, output directory, manifest | New |

MCU knowledge -- flash and SRAM ranges, register addresses, backup-register
enable -- lives in one table per MCU (STM32L432, STM32U375) in `SwdSession`.
It does not live in the procedures.

**Python surface.** It follows the Python Interface conventions: values
returned rather than output parameters, exceptions rather than Booleans,
`bytes` for data, sessions as context managers, and long operations releasing
the GIL with a progress callback.

```python
import tagcore

# The whole thing, as the CLI does it:
result = tagcore.recovery.capture("captures/", board=None, progress=print)
print(result.identity, result.manifest_path)

# Or the layers directly:
with tagcore.swd.open() as s:                   # attaches halted, before boot
    regs = s.read(0x40002850, 128)              # backup registers
    image = s.read_region("internal_flash")
    with s.external_flash(loader="AT25XE_PresTagv3") as xf:
        print(xf.identity)                      # JEDEC ID, SR1 as found
        data = xf.read(0, xf.size, progress=print)
# leaving the block detaches with hardware_reset by default
```

**Command-line tools.** Built on the same procedures:

- `tag-capture`: capture only, the first thing run on a returned tag.
- `tag-xflash`: `dump`, `identify`, and `rescue-erase`. `rescue-erase` refuses
  unless given a capture directory from the same tag (matched by chip UID), or
  `--no-capture` explicitly.

## Implementation sequence

Each step ends with a check on a bench tag, and each builds only on steps
already checked. Steps 1-3 need no loader changes.

**0. Baseline throughput.** *Done.* 32-bit memory reads through a base run at
101-104 KB/s at 2-4 KB per transfer (internal flash 256 KB in 2.59 s, SRAM1
48 KB in 0.47 s). A 4 MB external-flash dump is therefore bounded by SWD at
about 40 s, against CubeProgrammer's 70 s, and double buffering is dropped.

**1. Core control and attach-without-boot.** *Built, except the idle-current
check.* Halting uses `DHCSR` writes and core registers use `DCRSR`/`DCRDR`
through `LinkAdapt`'s existing debug-register access, so the base's
`FORCEDEBUG` handler is not needed. On the bench the core halted with PC equal
to the reset vector on every attach. The idle-current measurement after detach
is still to do.
Original plan: add halt, run, wait-for-halt,
core-register access and vector catch to `LinkAdapt`. Implement `SwdSession`
attach and detach as above. Check first whether the base's `FORCEDEBUG` handler
halts reliably; it carries a "this isn't working yet" comment in
`embedded/bases/common/src/stlink.c`, although CubeProgrammer halted the core
through a base on 2026-09-30.
*Check:*
- after attach, PC equals the reset vector;
- `RCC_CSR` still holds the flags from before the attach;
- after detach, `DEMCR` reads zero and the tag reaches its idle current. That
  last point is a measurement, per AGENTS.md: a leftover vector catch reads as a
  tag that never sleeps.

**2. Registers, internal flash and SRAM.** *Built and checked on STM32L432.*
Internal flash, OTP and option bytes were byte-identical across two captures
and a CubeProgrammer read. SRAM could not be cross-checked, because
CubeProgrammer lets the firmware run first. The U375 table is untested.
Original plan: `SwdSession` region reads and the MCU
tables of [MCU reference](#mcu-reference), with the capture directory and
manifest for steps 1-3 of a capture.
*Check:*
- the regions are byte-identical to CubeProgrammer reads of the same tag at the
  same moment, on an STM32L432 tag (where `tag_capture_state.py` fails today)
  and on an STM32U375 tag;
- the manifest decodes `FLASH_OPTR` correctly on both parts, including which
  SRAMs the attach erased and the watchdog selection;
- the ECC registers are captured before the flash read.

**3. ELF reader and generic ST-style call.** *Built.*
- `recovery/targetimage.*` is a minimal ELF32 reader.
- `recovery/sramcall.*` implements the traced convention. It downloads the
  image's SRAM segments with a `BKPT` trap word at the start of SRAM1, sets LR
  to the trap, MSP 1 KB past the image, R0-R3 to the arguments and xPSR to
  Thumb, then runs with `DHCSR.C_MASKINTS` so the tag's interrupts are never
  taken.
- `SwdSession` gained `Write`, `WriteCoreRegister`, `Run`, `Halt` and
  `WaitHalt`.
- `tag-xflash dump --loader <.stldr> -o <file>` calls `Init` once, then `Read`
  per 32 KB buffer, taking the part's base and size from the loader's
  `StorageInfo`.

Checked on a bench PresTag on 2026-10-01 (UID `20333050364150040063005F`):
- **Identical to CubeProgrammer.** A 4 MiB dump holding a logged run was
  byte-identical to `STM32_Programmer_CLI -el ... -u 0x90000000 0x400000` of
  the same part (SHA-256 `b51b10ef...`). It took 56.1 s at about 75 KB/s,
  against 57.4 s for CubeProgrammer and 101 KB/s for raw SWD reads (step 0).
  The gap to raw SWD is the per-call register setup and the loader's 2 ms
  flash wake, which `Serve()` removes.
- **The `resetCause` question is settled: it is CubeProgrammer's exit, not
  the loader.**
  - Backup registers captured before and after a `tag-xflash` session were
    identical.
  - Across a CubeProgrammer read of the same FINISHED tag, `resetCause` went
    from 2 to 1 and `external_blocks` from 14 to 60, rounded up to a page.
  - CubeProgrammer lets the firmware boot on exit, and that boot's recovery
    path rewrites `pState`. A `tag-xflash` session halts at the reset vector
    and ends in a plain reset, so the firmware runs only after the session.
- **A capture ends a short-period run.** On PresTag at sample periods under
  10 s (run-mode Stop 2), the reset that ends any SWD session is classified
  `EVENT_POWERFAIL` and the run goes to ABORTED. At 10 s and above (Standby
  between samples) the run carries on through the same reset. See
  `embedded/tags/design/restart-recovery.md`. Periods under 10 s are a bench
  convenience, not deployed, so this matters on the bench: capture a
  short-period test run only after stopping it.
Original plan: `TargetImage` and `SramCall`, then
call the existing `AT25XE_PresTagv3.stldr` `Init` and `Read` from the host.
*Check:*
- a 4 MB dump is identical to a CubeProgrammer dump of the same part;
- backup registers compared before and after a session that ends with
  `hardware_reset`. This settles whether the `resetCause` change comes from
  CubeProgrammer's exit or from something the loader session itself does.

**4. `Serve()` and `ExternalFlash`.** *Built; read path checked (2026-10-01).*
- **Command block:** `include/loader_service.h`, shared by loader and host.
- **Loader side:** `Serve(buffer, size)` in `embedded/loaders/common/src/loader_entry.c`,
  with `loaderFlashIdentity()` in the part driver. The ST entry points are
  unchanged.
- **Host side:** `recovery/externalflash.*` provides probe at open, `Read`,
  `EraseSector`, `Program` and `Close`.
- **Tool:** `tag-xflash dump` uses `Serve()` when the loader has it; `--st`
  forces the ST path.

Two departures from the plan above:
- The transfer buffer is passed to `Serve()` as an argument, not found by
  symbol, so the host chooses where it goes.
- The host zeroes the block before starting `Serve()`. The block is `.bss`,
  which is not downloaded, and SRAM survives a reset, so a magic left by an
  earlier session would otherwise read as ready.

Checked on the bench PresTag:
- A 4 MiB `Serve()` dump of a logged run was byte-identical to an ST-path dump
  of the same state, taking 53.8 s against 56.1 s. That is about 78 KB/s
  against step 0's 101 KB/s for raw SWD; SWD itself is the bottleneck.
- `detail[]` reported JEDEC `0x1F4708` and SR1 `0x00`, no block protection.
  The identity record (B1) stores the ID as `manufacturer << 16 | device1` =
  `0x1F0047`, because the firmware knows only those two bytes. Identification
  must compare those two bytes, not the whole word.
- Not yet checked: the read-only image refusing erase through `Serve()` (no
  tool issues an erase yet), and the loaders README bench sequence through
  `ExternalFlash` (the `-RW` image).

Original plan: add `Serve()` and the service block to
`embedded/loaders/common`, keeping the ST entry points unchanged, and implement
`ExternalFlash` on it.
*Check:*
- the dump matches step 3's;
- `detail[]` reports the JEDEC ID and SR1;
- the read-only image refuses erase through `Serve()`;
- the throughput is compared with step 0. The loaders README bench sequence
  (pattern, overwrite, refuse, restore) passes through `ExternalFlash`.

**5. Complete capture.** Add external flash to the capture, with the loader
named by argument. Retire `tag_capture_state.py`, or leave it as a thin wrapper
around `tag-capture`.
*Check:* a full capture of a PresTag, and a tag that is then booted and
downloaded normally, with its data intact.

**6. Python binding.** pybind11 over `SwdSession`, `ExternalFlash` and the
procedures. Port the loaders README bench sequence to a script beside
`tag_lifecycle_check.py`.
*Check:* the script passes on a PresTag and restores the tag to blank.

**7. Identification.** Generate and install the catalog, add `BOARD` to
`add_embedded_loader`, and implement hash lookup and the string scan.
*Check:*
- a tag running a released image is identified by hash;
- a development build by strings, marked heuristic;
- a mismatched explicit argument is reported.

**8. Rescue.** The `Rescue` procedure and `tag-xflash rescue-erase`. The order
is external erase with blank-check, then internal erase and reflash, then
`tag-reset` in a monitor session.
*Check:* on a bench tag with data in both flashes, the tag ends consistent: its
first new run downloads correctly. An interruption between the steps leaves the
harmless state described in Loader Runtime Design.

**9. Identity record (firmware).** *Done in the firmware, ahead of steps 4-8*
(`embedded/tags/design/next-release-todo.md` B1). It is in every family.
It was checked on hardware on a PresTag. What remains here is the host side:
identification (step 7) should read the record first.
Original plan: specify it with the session superblock, add
it to one family, and qualify that release. This is a separate firmware change,
gated on a firmware release.
*Check:* `tag_release_check.py` passes, and step 7 identifies the tag from the
record.

Steps 0-5 give a working replacement for CubeProgrammer and
`tag_capture_state.py`. Steps 6-9 can then be ordered by need.

## Risks and open questions

- **Base halt reliability.** Covered in step 1. If `FORCEDEBUG` is unreliable,
  halting falls back to writing `DHCSR` directly with `WriteDebug32`, which the
  monitor already does.
- **STM32U375.** The addresses are in [MCU reference](#mcu-reference). The
  monitor path is shared memory rather than DebugMonitor, so the vector-catch
  interaction may not apply. The loaders need a U3 variant before step 4
  applies there.
- **SRAM erased by the attach.** On a tag whose option bytes clear SRAM on
  system reset, no capture can recover SRAM, because this rig must attach under
  reset. Setting the "not erased" options on every tag at provisioning would
  close this. That is a policy decision, recorded here rather than made.
- **Long commands under USB timeouts.** A full rescue erase can run for over a
  minute. The host polls `ack` and `progress` rather than blocking, and the
  watchdog must allow for the loader's worst-case budgets.
- **Chip UID as the tag's identity.** The capture records the MCU's 96-bit UID,
  and `rescue-erase` matches it. Where a per-deployment record of UID to image
  should live remains the open question of Field Data Extraction.
