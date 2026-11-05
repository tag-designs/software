---
type: proposal
status: proposed
summary: Port the SWD capture, loader and rescue code from C++ tagcore/recovery to a recovery module in tagcore-rs, on tagcore-rs's own LinkAdapt rather than probe-rs, with the external-flash loaders shipped as a directory of images the tools load by name; ends CubeProgrammer use on the bench.
---

# SWD Recovery in Rust

The SWD recovery code -- capture without booting, the external-flash loaders'
`Serve()` sessions, and the offline rebuild -- can move from C++
`tagcore/recovery` to a `recovery` module in
[tagcore-rs](../../README.md). The tools move to
[commandline-rs](../../../../commandline-rs/README.md). The loaders stay
firmware: a directory of SRAM images, shipped in the firmware release, that the
tools pick by name.

**The port runs on tagcore-rs's `LinkAdapt`, not on probe-rs.** The C++ code is
a private subclass of the C++ `LinkAdapt`, and tagcore-rs's `LinkAdapt` is
already a line-for-line port of it, so the session layer ports one call for one
call (see [Why not probe-rs](#why-not-probe-rs)).

When it is done, STM32CubeProgrammer has no remaining use on the bench:
`tag_capture_state.py` is retired, and the loaders README's
`STM32_Programmer_CLI -el` recipe becomes an optional note.

## What uses CubeProgrammer today

Less than it may seem. The C++ capture and loader tools do **not** use it:
`SwdSession` drives the base through the C++ `LinkAdapt`, and `tag-xflash`
calls a loader's entry points itself. The remaining uses are:

| Use | What it is for | After this proposal |
| --- | --- | --- |
| `embedded/tools/tag_capture_state.py` | U375 bench captures (`mode=UR`) with the ELF and build files beside them | Retired: `tag-capture --sram --with-build` covers it, and halts before the firmware runs, which `mode=UR` does not ([SWD Capture and Recovery Library](../../../tagcore/design/swd-recovery.md#attaching-without-booting-the-firmware)) |
| `STM32_Programmer_CLI -el <loader>` in the [loaders README](../../../../../embedded/loaders/README.md#using-a-loader-with-stm32cubeprogrammer) | Reading external flash by hand | Kept as an optional note; the loaders keep the `.stldr` layout |

So the work is a port to Rust, which matters because the C++ host is being
retired tool by tool (decision [0035](../../../../../docs/decisions/0035-host-tagprogram-rs.md)),
plus a small extension to `tag-capture` that lets one script go.

## What moves

About 4,300 lines of C++ library and 800 lines of tools.

| C++ (`host/libraries/tagcore/recovery`) | Rust (`tagcore-rs/src/recovery`) | Notes |
| --- | --- | --- |
| `SwdSession`, `swdmcu` | `session.rs`, `mcu.rs` | Attach without booting, declared exit, core control, named-region reads; one table per MCU |
| `TargetImage` | `image.rs` | The `object` crate, already in the workspace through tagprobe-rs, replaces the hand-written ELF32 reader |
| `SramCall` | `sramcall.rs` | Download an image to SRAM and call a symbol with the traced convention and a timeout |
| `ExternalFlash` | `xflash.rs` | `Serve()` sessions over [`loader_service.h`](../../../../../include/loader_service.h); the block's layout is declared with `#[repr(C)]` and checked against the header by a build-time size and offset test |
| `IdentityRecord` | reuse | tagprogram-core's `find_identity_record()` already parses the record; move it into tagcore-rs so both use one parser |
| `statecapture`, `externalcapture` | `capture.rs` | The same directory layout and manifest, so existing captures, `decode_capture.py` and the C++ rebuild all read Rust captures |
| `CaptureSource` | stays C++ | It drives the unchanged SQLite writer through each family's decoder. Expose it through a new `rbd_*` module in `tagcore_c` and `tagcore-c-sys`, as the log writer already is (see [tagcore-c-sys](../../../tagcore-c-sys/README.md)) |
| `sha256` | `sha2` crate | |

| C++ tool (`host/commandline`, INTERNAL) | Rust tool (`host/commandline-rs`, internal) |
| --- | --- |
| `tag-capture` | `tag-capture`, plus `--sram` (U375 scratchpad) and `--with-build <dir>` (store the ELF and build files) |
| `tag-xflash` | `tag-xflash` (`probe`, `dump`, `read`; `rescue-erase` when the rescue is built) |
| `tag-sramcall` | `tag-sramcall` |
| `tag-rebuild` | `tag-rebuild`, through the `rbd_*` FFI |

The C++ library and tools are deleted once the [acceptance checks](#acceptance)
pass, not before.

## Why not probe-rs

tag-flash uses probe-rs, through tagprobe-rs, and it is the right tool there:
it brings the flash algorithms, the erase and verify, and the ELF loading.
Recovery needs none of that, and it needs one thing probe-rs is built to hide:
control of every write to the target.

- **probe-rs on a base is tagcore's `LinkAdapt` underneath anyway.**
  tagprobe-rs's `FieldSafeProbe` implements probe-rs's probe traits on top of
  `tagcore::LinkAdapt`, using only the subset of ST-LINK commands every field
  base supports. Running recovery through probe-rs would put probe-rs's session
  layer, and FieldSafe's emulation of DAP registers, between the code and the
  same `read_mem32`, `write_mem32`, `read_debug32`, `write_debug32` and
  `assert_reset` calls it would make directly.
- **The attach is a fixed sequence of writes, and the evidence depends on it.**
  Under NRST the session records `DHCSR`, `DEMCR` and the IWDG freeze bit as
  found, then writes `DHCSR` and `DEMCR.VC_CORERESET`, and only then releases
  reset. probe-rs's attach-under-reset makes its own reads and writes around a
  debug sequence, and a custom `ArmDebugSequence` overrides its steps but not
  their order. Proving that it touches nothing extra would mean auditing
  probe-rs on every upgrade. With `LinkAdapt`, the code is the sequence.
- **The exit has rules probe-rs does not know.** `Close()` must clear
  `DHCSR.C_MASKINTS` while halted, in a write that keeps `C_HALT` set, then
  clear `DEMCR`, then pulse NRST with `C_DEBUGEN` clear. If `VC_CORERESET` is
  left set, the next boot thinks a monitor is attached and the tag never sleeps.
  probe-rs's `core.run()` and session drop do their own writes to the same
  registers.
- **FieldSafe makes 8- and 16-bit accesses by read-modify-write of whole
  words.** That is safe for the SRAM buffers tag-flash uses, as its
  documentation says, but not for the peripheral registers a capture reads.
- **There is no speed difference.** Both move memory in 1 KiB
  `READMEM_32BIT` chunks.
- **probe-rs's main advantage does not apply.** It supports other probes
  (J-Link, CMSIS-DAP, stock ST-LINKs), but a tag is reached only through its
  base.

The one thing probe-rs would offer is a single attach path shared with
tag-flash. That has little value, because the two attaches are meant to
differ: tag-flash lets the image start, and recovery must not.

## The loaders

The loaders stay firmware, built by `embedded/loaders`, one SRAM image per
board and part, in both read-only and read-write builds. The `.stldr` layout is
kept, so CubeProgrammer can still use them. They ship as a directory:

- **In the firmware release archive**, under `loaders/`, beside the tag images.
  They change with the board's flash part, not with the host, and they are
  built by the same toolchain and reproducible-build flags. The release
  workflow's `distributed_firmware` target gains them.
- **Not in the host package.** A host tool cannot know which firmware a returned
  tag carries until it has captured it; the loader is chosen from the capture.

A tool finds a loader in this order:

1. `--loader <image>`;
2. `--loader-dir <dir>`;
3. `$TAG_LOADER_DIR`;
4. the build tree's `embedded/loaders` output, for a developer.

It finds the loader by the name the identity record gives, for example
`AT25XE_PresTagv3`, and uses the read-only image unless the operation writes, when it takes the `-RW` one. A loader's
version string, which names the board, part, build and commit, is recorded in
the capture manifest.

## Python

The [capture decisions](../../../tagcore/design/proposals/recovery-identification-rescue-python.md#python-binding)
call for the library to be easy to use from Python, and planned a pybind11
binding over the C++ classes. That binding was never built. Two options:

- **The Python tools call the Rust tools.** The bench scripts
  (`tag_rebuild_check.py`, `decode_capture.py`) already work with capture
  directories and subprocesses. This needs nothing new. **Recommended for
  now.**
- **A PyO3 binding over `tagcore::recovery`**, with the shape already planned:
  values returned, exceptions, `bytes`, context-manager sessions, and long
  operations that release the GIL. This is a later step, if a script needs
  per-call control.

## Risks

- **A different write order at attach or exit** changes what the capture shows
  or leaves a tag awake. Mitigation: port the sequence line for line, keep the C++
  comments, and compare a `LinkAdapt` trace of both tools on the same tag.
- **The loader block's layout.** A `#[repr(C)]` struct that drifts from
  `loader_service.h` would corrupt a `Serve()` session. Mitigation: a test that
  checks size and offsets against the header, through `bindgen` or a C
  `offsetof` table.
- **Idle current after a recovery session detaches has never been measured**
  ([recovery TODO](../../../tagcore/recovery/TODO.md#checks-never-run)). The
  port is a good moment to run it once, on both implementations.

## Acceptance

Each check runs on a bench tag of every loader board: PresTag, CompassTag and
UIUCTag (AT25XE), and IMUTag (GD5F and RV3028).

1. **Identical captures.** The C++ and Rust `tag-capture` give byte-identical
   capture directories from the same tag state, except for timestamps in the
   manifest. Each tool attaches without booting, so two captures in a row are
   of the same state.
2. **Identical external dumps.** `tag-xflash dump` with the same read-only loader
   gives the same file from both tools.
3. **Rebuild parity.** `tag_rebuild_check.py run` passes with the Rust
   `tag-capture` and `tag-rebuild`.
4. **The tag sleeps afterwards.** Idle current after a Rust session detaches
   matches the tag's idle current before the session.
5. **`tag_capture_state.py` is no longer needed.** A U375 scratchpad capture
   with `--sram --with-build` holds everything the script stored.

Then the C++ recovery library and its four tools are deleted, along with
`tag_capture_state.py`, and [Capturing a Tag](../../../../../docs/bench/capturing-a-tag.md)
names the Rust tools.

## Not in scope

- **The rescue procedure itself** (external erase, then internal erase and
  reflash, then `tag-reset`) is still unbuilt in C++. It is specified in
  [the rescue proposal](../../../tagcore/design/proposals/recovery-identification-rescue-python.md#rescue).
  It is built once, in Rust, after the port.
- **Identifying images without an identity record**, by hash and strings, is
  in the same proposal and is unchanged by this one.

## Estimate

About 2 to 3 days, plus a bench day for the acceptance checks:

- about 1 day for the session, MCU table and `SramCall`;
- about 1 day for `xflash`, capture and the tools;
- half a day for the `rbd_*` FFI and `tag-rebuild`;
- the loaders in the release archive.
