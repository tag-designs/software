---
type: decision
status: accepted
summary: The SWD capture and recovery library and its four tools (tag-capture, tag-xflash, tag-sramcall, tag-rebuild) are Rust, in tagcore-rs on its own LinkAdapt; the C++ library and tools and tag_capture_state.py are deleted, and only the offline rebuild (CaptureSource) stays C++, behind tagcore_c's rbd_* interface.
---

# 0045. Host: SWD capture and recovery move to Rust

Date: 2026-11-06

## Context

Decision [0042](0042-host-rust-default-release.md) left the SWD recovery
tools in C++ when the Rust tools became the release, because nothing replaced
them. They were the last C++ in `bin/internal` with a bench role, and
`tag_capture_state.py` still called STM32CubeProgrammer for U375 bench
captures with the build files beside them. Its connect-under-reset lets the
firmware start booting, so what it captured was not the tag as it stopped.

The plan is
[SWD Recovery in Rust](../../host/libraries/tagcore-rs/design/proposals/swd-recovery-in-rust.md).

## Decision

- **The library is a `recovery` module in tagcore-rs**, on tagcore-rs's own
  `LinkAdapt`, not on probe-rs, ported call for call from the C++ so the
  attach, exit and loader sequences do not change. Its design document moves
  with it: [SWD Capture and Recovery Library](../../host/libraries/tagcore-rs/design/swd-recovery.md).
- **The four tools are Rust**, in `host/commandline-rs`, with the names,
  options and output of the C++ tools, built into `bin/internal` by
  `rust_tools`. `tag-capture` gains `--sram` for the U375 scratchpad and
  `--with-build` to store the firmware build with the capture.
- **The offline rebuild stays C++.** `CaptureSource` drives the unchanged
  SQLite writer through each family's decoder; Rust reaches it through
  `rbd_rebuild()` in `tagcore_c`. `sha256`, `identityrecord` and `targetimage`
  stay in `tagcore/recovery` for it and for the legacy qtprogram.
- **One identity-record parser.** Tag Programmer and `tag-flash` use
  `tagcore::recovery`'s.
- **The loaders ship in the firmware release** under `firmware/loaders/`, so a
  capture on another machine finds them by name.
- **Deleted**: the C++ session, MCU table, SRAM call, external-flash and
  capture code, the four C++ tools, and `tag_capture_state.py`.

## Alternatives considered

- **Port onto probe-rs**, as `tag-flash` is. Recovery needs the opposite of
  what probe-rs provides: no flash algorithms, and an attach that halts at the
  reset vector and a declared exit, which probe-rs's session hides.
- **Keep the C++ tools.** They worked, but held the libusb C++ link layer in
  the default build for four internal tools while the rest of the host moved.
- **Port `CaptureSource` too.** It is the SQLite writer's per-family
  decoders; porting it would duplicate the writer the download path shares,
  against the rule that Rust reuses that C++ through FFI.

## Evidence

On 2026-11-06, Rust against C++ on the same tag state, all at fw `3a49003b`:

- **PresTag** (`20333050364150040063005F`, 2.496 V): captures byte-identical
  but for the RTC time and manifest timestamps, blank and after runs of 61 and
  1,801 samples; full 4 MB `tag-xflash` dumps through `Serve()` and the ST
  entry points identical; `tag-sramcall` RV3028 reads identical;
  `tag_rebuild_check.py run` with the Rust tools passed (1 s period, 30
  minutes); idle current unchanged after a session.
- **IMUTag** (`00303143433650090059002E`, 3.693 V): NAND captures and
  `tag-xflash nand` identical; the rebuild check passed; `--with-build`
  matched the image over 60,440 bytes.
- **CompassTag** (`203633324B4250060022005E`, 2.496 V): captures and dumps
  identical; the rebuild check passed; idle 0.2196 uA before, 0.2143 uA after.
- **UIUCTag breakout** (`2036354B3032500800520028`): internal captures
  identical and 512 KB reads identical, but long loader sessions failed with
  both tools on its `tag-breakout-base-l432-v1` base, a base fault recorded in
  the [tagcore-rs TODO](../../TODO.md#tagcore-rs).
- C++ and Rust `tag-rebuild` gave identical SQLite on every capture, and the
  data tables matched the download.

## Consequences

- STM32CubeProgrammer has no remaining bench use; the loaders keep their
  `.stldr` layout, so it still works as a manual fallback.
- A capture taken by either implementation is read by `tag-rebuild`.
- New recovery work, starting with the rescue erase, is built once, in Rust.
