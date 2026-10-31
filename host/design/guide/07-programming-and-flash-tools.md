---
type: design
status: current
summary: Chapter 7 of the Host Software Guide - how the host programs a tag's firmware and a base's (tagprobe over probe-rs, the two ST-LINK drivers and how a base's identity chooses between them, the erase plan and the program sequence, programming a base through its ROM bootloader over USB DFU) and the two command-line tools, tag-flash and base-flash, that expose it.
---

# 7. Programming tags and bases

Programming puts a firmware image into a tag's internal flash. It is the
one job the host does without the tag's firmware (there may be none), and
the one the Rust port changed in kind: the C++ Tag Programmer runs ST's
`STM32_Programmer_CLI`, which needs the 1.7 GB STM32CubeProgrammer
installed on every station; the Rust side programs through the
[probe-rs](https://probe.rs) library and needs nothing installed. This
chapter assumes chapter 3 (SWD, bases, the exclusive ownership of a base).

## tagprobe

`libraries/tagprobe-rs` (crate `tagprobe`) wraps probe-rs 0.32 for the four
jobs the host used to give the CLI: listing bases, the **connect check**
(device ID, part name, target voltage), **programming** an ELF with the
right erase, and **reading flash back**. It also programs **bases** over USB
DFU, which has nothing to do with probe-rs and is described below.

```text
tagprobe::list()  identify(&base)  connect_check(&base)  chip_for(device_id, flash_bytes)
tagprobe::program(&base, driver, chip, elf, erase, on_progress)   erase_all()   read_memory()
tagprobe::dfu::{list, flatten_elf, program}
```

Every call opens the base, does its work and closes it; nothing is held
between calls, so any thread may call it, one call per base at a time, and
the caller owns the exclusion against a `tagcore::Tag` on the same base.

### Two drivers, chosen by the base's identity

probe-rs has its own ST-LINK driver, and the bases emulate an ST-LINK, but
the emulation in bases flashed before mid-2026 gets two things wrong that
probe-rs depends on: a 1-byte memory read returns a 1-byte reply (probe-rs
waits for a second byte), and the `READ_DAP_REG` command returns stale data.
Bases in the field cannot be reflashed, so the crate carries two drivers and
chooses per base:

```text
identify(base) ──► Some("base=...;fw=...") ──► Driver::Stock      probe-rs's own ST-LINK driver (src/lib.rs)
               └─► None (no answer)        ──► Driver::FieldSafe  FieldSafeProbe (src/fieldsafe.rs)
```

The `F6 99` identify command was added to the base firmware in the same
change that fixed the two faults, so an answer is a sound test for "has the
fixes". The **field-safe driver** implements probe-rs's `DebugProbe` and Arm
debug-interface traits over `tagcore`'s `LinkAdapt`, using only the ST-LINK
commands every base answers correctly: it answers probe-rs's debug-port and
access-port register reads from a table of the part's real values, ignores
the writes (memory never goes through those registers), and does all memory
access with the base's 32-bit memory commands, patching whole words for
8- and 16-bit writes. It is safe for SRAM and flash-algorithm buffers,
which are all probe-rs writes bytewise, and is not a general debugger.

One platform rule hides here: the stock driver is opened through
`probe_rs::probe::stlink::StLinkFactory`, never probe-rs's general `Lister`.
The lister enumerates CMSIS-DAP probes through hidapi, whose macOS
implementation binds to the first thread that used it; Tag Programmer runs
each programming job on a new worker thread, and the second job in a session
crashed the process until this was found.

### The part, the erase and the sequence

`chip_for(device_id, flash_bytes)` maps the device ID (0x435 STM32L432,
0x454 STM32U375) and the flash size to a probe-rs target name:
`STM32L432KC` or `STM32L432KB` (the UIUCTag's 128 KB part; choosing the KC
for it would make probe-rs's flash algorithm address flash that does not
exist) or `STM32U375RG`. The flash size comes from the tag's own `TagInfo`
when its firmware answers, otherwise from the image's `__flash0_size__`
symbol, and `tag-flash` reads it from the part's register in a session of
its own.

`program()` runs one probe-rs session under reset:

1. Erase as the `Erase` plan says: `ImagePages` (only the sectors the image
   covers, what the CMake `-download` targets always did), `Range` (Tag
   Programmer passes everything below the calibration page, so a CompassTag's
   stored calibration survives a reflash), or `All` (chip erase).
2. Download the ELF with verification, in the same session, so no reset
   falls between erase and program.
3. On an STM32L4, read `FLASH_SR` and clear `PEMPTY` only if it is set. The
   bit is write-one-to-toggle, so writing it when clear would set it. With it
   set after a mass erase the L432 boots its ROM bootloader instead of the
   image until power is removed; this step is why Tag Programmer's "please
   power cycle the tag" prompt is now rare.
4. Load SP and PC from the vector table and resume the core, as the CLI's
   `-g 0x08000000` did. The new image is running when the call returns.

Progress reaches the caller as `Progress::Step` (erasing, programming,
verifying) and `Progress::Bytes`, on the caller's thread inside probe-rs's
loop, so the callback must not block. The session uses the Arm default debug
sequence rather than probe-rs's STM32 one, which would set `DBGMCU_CR`'s
debug-in-stop bits and change the tag's sleep behaviour after detach; the
power measurements that justified this are in the
[probe-rs evaluation](../../../docs/investigations/2026-10-probe-rs-evaluation.md).

### The erase plan comes from the image

Which erase is right depends on the image, not on the operator. Tag
Programmer's model (`tagprogram-core/src/model/flashplan.rs`, a port of
qtprogram's `flashplan.cpp`) reads the ELF's linker symbols to decide: an
image with a calibration page keeps it (`BelowCalibration`), an image whose
layout it cannot read gets `None` with a reason, and the rest erase all. The
same module is what `tag-flash program --erase plan` uses, which is why the
`tag-tools` crate depends on `tagprogram-core`.

## Programming a base: DFU

A base's own microcontroller (STM32F042, STM32C071 or STM32L432, by board)
starts its ROM bootloader when the base is plugged in with its button held.
The bootloader enumerates as `0483:df11` and speaks **DfuSe**, ST's dialect
of the USB DFU standard: alternate setting 0 is internal flash with a memory
map in its string descriptor, block 0 of a download carries commands (set
address, erase), and a zero-length download ends the transfer and starts the
image. `tagprobe::dfu` does this on the `dfu-core` and `dfu-nusb` crates:
`list()` the devices in DFU mode, `flatten_elf()` the image to its load
bytes (as `objcopy -O binary` would), and `program()`, which writes alternate
setting 0 only, after checking that it is internal flash, so option bytes
are never touched. It is the equivalent of `dfu-util -a 0 -s 0x08000000:leave`.

## The tools

### tag-flash

`commandline-rs/src/bin/tag-flash.rs` is the command line over `tagprobe`,
and what the CMake `<target>-download` and `-erase` targets, `flash_release.py`
and `decode_scratchpad.py --read` run instead of `STM32_Programmer_CLI`
(the CMake side falls back to the CLI where cargo is missing and the tools
are not on `PATH`, or with `-DEMBEDDED_PROGRAMMER=CUBEPROGRAMMER`).

| Command | Does |
| --- | --- |
| `tag-flash list` | The bases on USB: serial, `bus:device`, identity |
| `tag-flash [-b SERIAL] info` | The connect check: device, part, flash size, supply voltage |
| `tag-flash [-b SERIAL] program IMAGE.elf [--erase image\|plan\|all]` | Erase, program, verify and start, in one session |
| `tag-flash [-b SERIAL] erase` | Mass erase; an L432 then boots its ROM bootloader until programmed |
| `tag-flash [-b SERIAL] read ADDRESS LENGTH -o FILE` | Read memory under reset (the scratchpad, for instance) |

Unlike the monitor tools, `-b` takes the base's USB serial, which survives
replugging. Every command but `list` resets the tag. Neither `program` nor
`erase` touches the external flash log; the flashing protocol in the root
`AGENTS.md` says to erase that first with `tag-reset`, which asks the
firmware to do it.

### base-flash

`commandline-rs/src/bin/base-flash.rs` programs a base through DFU and is
what the CMake `-dfu` targets run: `base-flash list`, `base-flash program
IMAGE.elf`, with `--wait` to start before the base is plugged in. It succeeds
only when the base comes back on USB as an ST-LINK reporting the identity
string the image carries. On Linux it needs the `0483:df11` line of
`host/linux/70-ultralight-tags.rules`. The three distributed bases' images
are released with the tags' so a bench machine can reflash a base without
building firmware.

## Rules for callers

- **Never hold the base from `tagcore` and `tagprobe` at once.** Detach the
  `Tag` before any tagprobe call and attach again afterwards. Both open the
  same USB device; interleaved commands corrupt each other.
- **Expect the monitor to need a moment afterwards.** After probe-rs releases
  a field base the tag is booting, and `tagcore`'s first debug-register reads
  can fail with `0x81`; `tagcore` rate-limits those messages and the
  applications retry the attach.
- **Do not open probes through probe-rs's `Lister`** (above).
- **Never start a tag after programming.** Both Tag Programmers end their
  session with the tag IDLE after its self tests. A start belongs to a
  deployment, and on a PresTag a start followed by a clock set hung the
  firmware ([decision 0035](../../../docs/decisions/0035-host-tagprogram-rs.md)).

## Where to read next

- [`libraries/tagprobe-rs/README.md`](../../libraries/tagprobe-rs/README.md)
  and its [design](../../libraries/tagprobe-rs/design/architecture.md): the
  API table and the field-safe driver's register tables.
- The [probe-rs evaluation](../../../docs/investigations/2026-10-probe-rs-evaluation.md):
  what probe-rs's source says, what the spike found on each base, and the
  power measurements.
- The [plan for programming without CubeProgrammer](../../commandline-rs/design/proposals/programming-without-cubeprogrammer.md):
  how `tag-flash` and `base-flash` replaced the CLI in CMake and the scripts,
  and the bench checks.
- Tag Programmer's use of all this: chapter 11 and
  [its engine design](../../applications/tagprogram-rs/design/engine.md).
