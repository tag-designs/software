---
type: decision
status: accepted
summary: A returned tag's external flash is read by a read-only loader running from SRAM, never by a recovery firmware, after the SRAM and internal-flash capture, with one loader image per board.
---

# 0022. Field extraction: read external flash with an SRAM loader, not a recovery firmware

Date: 2026-09-27

Chosen when field recovery was first designed, and built as
`embedded/loaders` (first loader `AT25XE_PresTagv3`, 2026-09-30; see
[0016](0016-field-extraction-first-loader-settled.md)). Moved from
[Field Data Extraction](../../embedded/tags/design/proposals/field-data-extraction.md),
which keeps the parts still proposed.

## Context

A tag comes back from a field site months after it was flashed. The recorded
data must be extracted whatever state the tag is in, and enough recorded
alongside it to understand what went wrong. Where the bulk of recorded data is
on external flash, something has to drive that flash over SWD.

Where the bulk of recorded data is on external flash, the extraction path can
reuse STM32CubeProgrammer's external loader mechanism: a small binary (`.stldr`)
that the programmer downloads into the tag's SRAM and calls, exporting a
`StorageInfo` descriptor plus `Init`, `Read`, `Write`, `SectorErase` and
`MassErase`. Reads are function calls, so the address space is fictional and the
mechanism is not restricted to memory-mapped QSPI/OSPI -- it works for plain SPI
NOR and NAND on an ordinary SPI peripheral, which is what the tags use.


## Decision

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
[Rescue erase](../../embedded/loaders/design/loader-runtime.md#rescue-erase).

### Raw reads for NAND

For the GD5F SPI-NAND parts, prefer reading raw pages *including the spare area
and ECC bytes*, and do bad-block skipping and ECC correction on the host. A
loader that corrects and skips internally can discard information irrecoverably
-- and when the failure under investigation is itself in the bad-block map or
the ECC path, the loader would be hiding exactly the evidence that matters.

Revised by [Offline Log Reconstruction](../investigations/2026-10-offline-log-reconstruction.md): the
GD5F's on-die ECC algorithm is not in the source, so the host cannot correct a
raw page itself. A capture should read each used page raw **and** through
on-die ECC, recording the ECC status. The firmware drops a page whose ECC
fails, and only the on-die read shows which pages those are.

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

## Alternatives considered

- **A dedicated dumper firmware** that reads external flash and sends it out
  over the monitor. Rejected: it overwrites internal flash, as above.
- **ST's External Memory Manager** to generate the loader. Rejected when the
  first loader was built; see [0016](0016-field-extraction-first-loader-settled.md).

## Consequences

The loaders are in [`embedded/loaders`](../../embedded/loaders/README.md), with
their runtime rules in
[Loader Runtime Design](../../embedded/loaders/design/loader-runtime.md). The
host library that drives them in place of STM32CubeProgrammer, and enforces the
capture-first order, is
[SWD Capture and Recovery Library](../../host/libraries/tagcore/design/swd-recovery.md).
`tag-capture` reads SPI NAND both raw and through on-die ECC, as the revision
above requires.
