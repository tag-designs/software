# Field Data Extraction

Status: partly implemented. The external-flash loader is built and validated on
hardware for one board, PresTagv3 with an AT25XE321D; see
[External Flash Loaders](../embedded/loaders/README.md) and
[Loader Runtime Design](../embedded/loaders/design/loader-runtime.md). The
session superblock (Gap 1) and the field failure record (Gap 2) remain
proposals. [What the first loader settled](#what-the-first-loader-settled)
records which of this document's expectations held and which did not.

## Purpose and scope

A tag comes back from a field site months after it was flashed. Two things
matter, in this order:

1. **Extract the recorded data**, whatever state the tag is in.
2. **Have enough recorded alongside it to understand what went wrong.**

The risk addressed here is not that data is unreachable. It is that data is
reachable and *silently misinterpreted*, because nothing in the bytes says which
firmware wrote them or how they are laid out.

Establishing which firmware an image is, and being able to rebuild it, is
covered separately in
[Tag Firmware Build Reproducibility](tag-build-reproducibility.md). The two
documents meet at the image hash: a capture that includes internal flash
contains the bytes of the image, so hashing them identifies the build without
trusting any embedded metadata.

## What exists today

**The internal-flash marker log is the field mechanism.** `recordState()` in
[`embedded/tags/common/core/src/persistent.c`](../embedded/tags/common/core/src/persistent.c)
writes a `t_StateMarker` into the `.persistent` section on every state
transition: epoch, state, internal and external page counts, supply voltage,
temperature, and the reason. It survives power loss and works on every tag
family. The `detail` word is STM32U3-only, taken from padding the 128-bit flash
row requires; the STM32L4 record has no slack.

**The retained scratchpad is not a field mechanism.** It is a bench debugging
tool: SRAM2 page 3 on STM32U375 targets only, enabled with `-DTAG_SCRATCHPAD=1`.
It is excellent for attach storms and reset loops on a desk, and it must not be
load-bearing for anything a returned tag has to tell us. Any diagnostic that
matters in the field belongs in internal flash.

**Capture already works on STM32U375 tags.**
[`embedded/tools/tag_capture_state.py`](../embedded/tools/tag_capture_state.py)
connects under reset and stores SRAM, the writable part of internal flash, and
the RTC backup registers, reading region bounds from the ELF it is given. On
STM32L432 tags only the internal-flash regions capture today: the SRAM and
backup-register steps fail
([open issue](../embedded/loaders/design/loader-runtime.md#open-issues)).

## Gap 1: the recorded data is not self-describing

This is the priority-1 problem.

Every page written by an IMUTag is prefixed by:

```c
typedef struct {
    int32_t epoch;
    uint16_t millis;
    int16_t rawtemp;
} t_ImuTagPageHeader;
```

Eight bytes. No magic, no format version, no tag-family identifier. Everything
needed to interpret the page body -- `IMUTAG_IMU_SAMPLES_PER_SUPERFRAME`,
`IMUTAG_SUPERFRAMES_PER_PAGE`, the layout of `t_ImuTagImuSample` and
`t_ImuTagAuxSample` -- is a compile-time constant in
[`include/imutag_log_format.h`](../include/imutag_log_format.h), present nowhere
in the data.

Three consequences:

- A dump is only decodable by a host tool that still matches the firmware that
  wrote it. Change the superframe count, or add a field to the aux sample, and
  old and new dumps become indistinguishable by inspection. The host decodes
  both. One of them is wrong, with plausible values and wrong timestamps -- a
  worse outcome than failing to decode.
- Nothing in the bytes says whether a dump is an IMUTag, PresTag or UIUCTag.
- Correct decoding therefore depends on identifying the firmware, which makes
  build provenance a prerequisite for reading data. It should not be.

### Proposal: a session superblock

One self-describing record written where the data region begins, covering the
session that follows:

| Field | Purpose |
| --- | --- |
| `magic` | Identifies the block without external context |
| `format_version` | Lets a decoder reject or adapt, rather than guess |
| `tag_family` | Distinguishes IMUTag / PresTag / UIUCTag payloads |
| `header_size`, `sample_size`, `samples_per_frame`, `frames_per_page` | The layout constants that are currently compile-time only |
| `image_sha256` | Ties the session to an exact firmware image |
| `git_sha`, `dirty` | Human-readable provenance |
| `session_epoch` | When logging started |
| `config_digest` | Which configuration produced this session |

With this, a raw dump decodes standalone, indefinitely, with no reference to a
build -- which is the whole point, and which removes the need to identify a
build in order to read its data.

Two riders. It cannot help tags already deployed, so for those the mapping from
tag to image must be recorded externally before they fly. And while the format
is being versioned: `int32_t epoch` overflows in January 2038. A
`format_version` field is what makes widening it survivable later.

## Extracting external flash: STM32CubeProgrammer external loaders

Where the bulk of recorded data is on external flash, the extraction path can
reuse STM32CubeProgrammer's external loader mechanism: a small binary (`.stldr`)
that the programmer downloads into the tag's SRAM and calls, exporting a
`StorageInfo` descriptor plus `Init`, `Read`, `Write`, `SectorErase` and
`MassErase`. Reads are function calls, so the address space is fictional and the
mechanism is not restricted to memory-mapped QSPI/OSPI -- it works for plain SPI
NOR and NAND on an ordinary SPI peripheral, which is what the tags use.

### Why this rather than a recovery firmware

The obvious alternative is to flash a dedicated dumper image that reads external
flash and sends it out over the monitor. **That overwrites internal flash**, and
internal flash is where the `t_StateMarker` marker log, the persistent
configuration and the NAND map live. It would destroy the evidence for goal 2 in
the course of serving goal 1, and it would do so silently.

An external loader runs entirely from SRAM and leaves internal flash untouched.
For a tag returned from the field that is the deciding property.

### Capture order is therefore fixed

The loader occupies SRAM, destroying whatever the application left there. So:

1. `tag_capture_state.py` first -- SRAM, writable internal flash, RTC backup
   registers.
2. The external loader second.

Reversing these loses SRAM state and looks like it worked. This belongs in the
recovery procedure as a rule, not as a note.

### Read-only by default

`STM32_Programmer_CLI` will happily erase external memory through a loader. A
loader used for field recovery should implement `Read` and `StorageInfo` and
stub `Write`, `SectorErase` and `MassErase` to fail, so the forensic tool is
incapable of destroying the thing it was brought in to recover.

Implemented as two images from one source: `<PART>_<Board>.stldr` is read-only
and is built without the erase and program code at all, and
`<PART>_<Board>-RW.stldr` erases and programs, each verified by read-back, for
rescue and bench testing. External erase is a rescue operation, always paired
with an internal erase, and the order matters; see
[Rescue erase](../embedded/loaders/design/loader-runtime.md#rescue-erase).

### Raw reads for NAND

For the GD5F SPI-NAND parts, prefer reading raw pages *including the spare area
and ECC bytes*, and do bad-block skipping and ECC correction on the host. A
loader that corrects and skips internally can discard information irrecoverably
-- and when the failure under investigation is itself in the bad-block map or
the ECC path, the loader would be hiding exactly the evidence that matters.

### One loader per board; sharing is at the source level

There is no way to have fewer loaders than boards. A `.stldr` is fully linked
to run from RAM -- the programmer downloads the image into SRAM and calls its
entry points -- so the artifact is bound to one board's pin assignment, memory
part, clock setup, power-enable GPIO, and to a link address and size that fit
that target's SRAM map. Nothing about that is shareable between a STM32L432
board and a U375 one.

What is shared is source, though less of it than this document first expected.
The loader runtime (clock, delay, SPI, entry points) is shared across loaders in
`embedded/loaders/common/`. Between a loader and the firmware, the shared layer
is the part's command set -- opcodes, register bits and measured timing budgets
-- in a dependency-free `<part>_commands.h` beside the firmware driver
(`at25xe_commands.h` is the first). The drivers themselves are not shared; the
next section says why.

So the build is a matrix over boards, like the firmware itself, and the
per-board work is a short configuration rather than a driver: pins, board
bring-up, part selection, descriptor.

### What the first loader settled

Building `AT25XE_PresTagv3` answered the questions this section used to leave
open, and overturned three of its expectations. The detail is in
[Loader Runtime Design](../embedded/loaders/design/loader-runtime.md).

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

ST's External Memory Manager was considered and does not fit. Its custom-driver
configuration is XSPI vocabulary -- dummy cycles, single/dual/quad modes, DQS,
instruction and address widths -- while every external flash in this tree hangs
off SPI1 (`TAG_SPI1_DEVICE_DEFAULTS` in
[`spi_bus.h`](../embedded/tags/common/core/inc/spi_bus.h)). Its parameter list
is still a useful checklist of what a loader's `Init` must establish -- JEDEC
ID, capacity, reset method, read opcode, dummy cycles, chip-select timing --
and if a future board puts flash on OCTOSPI it becomes worth revisiting, since
a generated loader would beat a written one. Note that ST's generated loaders
are read/write by design, so the read-only stubbing above would have to be
applied deliberately.

**STM32CubeProgrammer is a poor driver for this.** Its contract is
undocumented, it rejects a malformed loader silently, and its sector-number
erase is ambiguous between internal and external memory when a loader is
loaded. It also knows nothing of capture-first or external-before-internal
ordering. A host tool on `tagcore`, using the same loader images, is the next
step; see [Where to start](#where-to-start).

## Gap 2: the field failure record

The marker log is the right place and mostly does the job. Three gaps.

**The log stops silently when full.** `recordState()` returns without recording
once `offset >= sEPOCH_SIZE`, and the only notice is `tagScratchWord("ESLF",
...)` -- which reaches the scratchpad, which is not present in the field. A tag
that filled its marker log and then had an interesting failure is
indistinguishable from a tag that simply stopped transitioning. Reserving the
final slot for an overflow marker, or carrying a dropped-transition count that a
later write can fold in, would make the distinction visible in a capture.

**Resets and faults leave no marker.** A hard fault, a watchdog bite and a
brownout are all invisible in the flash record; the tag simply reappears in an
earlier state. Latching the reset cause at boot from `RCC_CSR` and the `PWR`
flags, and recording it as a marker reason, separates those cases. This is
cross-family and cheap, but it costs a flash write, so it is worth recording
only when the cause is not an ordinary power-on -- an attach storm should not
burn the log.

**No build identity in the flash record.** Covered by the superblock when the
data region carries one; otherwise one marker at session start carrying a
truncated image hash serves the same purpose.

## Where to start

The first loader exists. What remains is independent work.

**A host tool to drive the loaders**, designed in
[SWD Capture and Recovery Library](../host/libraries/tagcore/design/swd-recovery.md).
The base firmware already implements the
ST-LINK core-register, run and debug-register commands that the calling
convention needs, and `tagcore`'s `LinkAdapt` already provides attach under
reset and memory access. A host tool can enforce the capture-first and
external-before-internal ordering, feed dumps straight to the decoders, and
control how the core is left afterwards, which CubeProgrammer does not (see the
`resetCause` item under
[Open issues](../embedded/loaders/design/loader-runtime.md#open-issues)).

**More loaders.** The per-board work is a short configuration; the
[add-a-loader checklist](../embedded/loaders/README.md#adding-a-loader) covers a
new board, a new part and a new MCU.

**Moving the storage drivers off the kernel API** is still defensible on its
own terms -- a device timing delay is not a scheduling decision -- but it is no
longer a prerequisite for anything here, and it is not sufficient to make a
driver usable from a loader.

The data-format work -- the session superblock of Gap 1 -- is independent of
all of this and gated on a different question: where the bulk of recorded data
lives. That is the first open question below, and it decides whether the
superblock belongs on external flash or internal.

## Open questions

- **Where does the superblock live?** If the bulk of recorded data is on external
  flash, the superblock belongs there, written through the same path that writes
  pages, and the internal-flash capture path is serving goal 2 rather than goal
  1. This decides which piece of work is actually urgent.
- **Which loaders are needed next?** The first, `AT25XE_PresTagv3`, is done.
  The other AT25XE boards are a configuration each; MX25R, MX25L and the GD5F
  NAND parts each need a part driver.
- **Where is the per-deployment record** mapping a physical tag to the image hash
  it was flashed with, for tags deployed before a superblock exists?
- **Is a reset-cause marker worth its flash write**, given endurance and energy
  on a 12 mAh cell?
- **Fix `int32_t epoch` now**, while the format is being versioned, or accept the
  2038 boundary?
