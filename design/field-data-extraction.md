# Field Data Extraction

Status: proposal. Nothing here is implemented.

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

**Capture already works.**
[`embedded/tools/tag_capture_state.py`](../embedded/tools/tag_capture_state.py)
connects under reset and stores SRAM, the writable part of internal flash, and
the RTC backup registers, reading region bounds from the ELF it is given.

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
incapable of destroying the thing it was brought in to recover. A separate
writable loader can exist for production use if one is ever wanted.

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

What is shared is source. The repository already has the seam:
[`storage_spi.h`](../embedded/tags/common/storage/inc/storage_spi.h) wraps SPI
behind `TagSpiDevice` with inline framing helpers, and the drivers in
`embedded/tags/common/storage/src/` sit on top of it. There are five parts in
the tree -- `at25xe`, `mx25r`, `mx25l`, `mx25u12843`, `gd5f` -- and a
freestanding SPI backend under that seam would let every loader compile the
same command sequences the firmware uses, so a driver fix reaches both.

So the build is a matrix over boards, like the firmware itself, and the
per-board work is a short configuration rather than a driver: pins, clocks,
power enable, part selection, link address.

This is a refactor rather than a recompile: the drivers currently include
`hal.h`, `rtc_api.h`, `debug_log.h` and `phase_probe.h`, and a loader has no
ChibiOS, no HAL and no application startup. `Init` must bring up its own clocks,
SPI and any power-enable pin from reset state.

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

### What to verify before committing to this

- **SRAM budget and link address.** The loader is linked to a fixed address and
  runs with its stack and the programmer's transfer buffer alongside it, so both
  the address and the size are per-board facts to establish -- tight on
  STM32L432's 64 KB, comfortable on the U375 parts.
- **Board bring-up from cold.** Whether each board's external flash can be
  reached without the application's power sequencing, and what `Init` must
  replicate.
- **Provenance.** A `.stldr` is per board, and a mismatched loader reads
  plausible garbage without complaint. Loaders belong in the image archive,
  keyed to the board, and built by the same CI job.

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

## Open questions

- **Where does the superblock live?** If the bulk of recorded data is on external
  flash, the superblock belongs there, written through the same path that writes
  pages, and the internal-flash capture path is serving goal 2 rather than goal
  1. This decides which piece of work is actually urgent.
- **Which loaders are needed first?** One per board, but the flash command set is
  shared; the first one built will show how much of the existing driver code
  survives being made freestanding.
- **Where is the per-deployment record** mapping a physical tag to the image hash
  it was flashed with, for tags deployed before a superblock exists?
- **Is a reset-cause marker worth its flash write**, given endurance and energy
  on a 12 mAh cell?
- **Fix `int32_t epoch` now**, while the format is being versioned, or accept the
  2038 boundary?
