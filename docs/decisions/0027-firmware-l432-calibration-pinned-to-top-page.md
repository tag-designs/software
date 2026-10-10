---
type: decision
status: accepted
summary: On STM32L432 tags that store calibration (CompassTag), the calibration table is pinned to the top flash page while .persistent keeps floating, so an upgrade that erases every other page keeps calibration at no log-capacity cost.
---

# 0027. Firmware: L432 calibration pinned to the top flash page

Date: 2026-10-10

Reverses the rejection of 2026-10-05 recorded in
[the CompassTag TODO](../../embedded/tags/families/CompassTag/TODO.md), for
calibration only. The reasons given there for not pinning `.persistent` still
stand.

## Context

`STM32L432xC.ld` placed `.calibration` at the first page boundary after the
code, so the table moved whenever the image crossed a page: `0x0800a800` at
`4160d1e`, `0x0800b000` at `fw-v0.5`, with 200 bytes left before the next move
at `fw-v0.6`. A calibrated board that is upgraded across such a move reads an
unwritten page, or leftover code bytes, as its constants, and nothing reports
it.

On 2026-10-05 the full U375 arrangement was built for the L432: calibration and
the NAND map pinned at the top of flash, and `.persistent` pinned from a 64 KB
code ceiling. It was rejected because it cost 10-15% of internal log capacity
on all four L432 targets to protect one, and because the agreed upgrade path was
a mass erase, which destroys calibration wherever it sits.

CompassTag is now being prepared for distribution, and its calibration record
is gaining accelerometer constants. Calibrated units will be upgraded after
they leave the bench, and recalibrating each one at every upgrade is a cost the
mass-erase policy did not have to weigh when every unit was on the bench.

## Decision

- `STM32L432xC.ld` places `.calibration` at the top `TAG_CALIBRATION_PAGES`
  flash pages, a fixed address. The default is zero.
- The CompassTag family sets `TAG_CALIBRATION_PAGES=1` from its `family.mk`,
  putting the table at `0x0803f800` (page 127) on all three variants.
- `.persistent` still starts at the first page after the code and now ends at
  the calibration pages. Its start still moves with code size.
- The upgrade for a calibrated L432 tag is `flash_release.py
  --keep-calibration`: erase pages 0 through the one below
  `__calibration_start__`, then program, in one programmer invocation. That
  clears the moving `.persistent`, so stale stored configuration or state is
  never read, and keeps calibration.
- The release that introduces this layout also changes the calibration record
  format, so it is installed with a mass erase (`--erase`) and recalibration.
  That is a one-time step.

## Alternatives considered

- **Pin `.persistent` as well (the 2026-10-05 change).** It would make a plain
  program safe, but it costs 10-15% of log capacity on every L432 target.
  Erasing the floating region is cheaper.
- **Pin calibration on every L432 target.** It charges BitTag, PresTag and
  UIUCTag a page each for a region they never use. With the zero default their
  layout is unchanged.
- **Keep mass erase for every upgrade.** Correct, but it costs a recalibration
  per unit per upgrade once units are distributed.
- **Only add an ASSERT on the gap between code and calibration.** It turns a
  silent move into a build failure, but the table still has to move eventually.
  The pinned layout includes an equivalent ASSERT anyway.

## Evidence

Built from the same tree before and after the change:

- BitTag, PresTag, PresTagRaw, BitPresTag, BitPresTagMX25R, BitTagNG and UIUCTag
  produce byte-identical `.bin` images. Their `.persistent` bounds are
  unchanged; only their empty calibration symbols moved, to the end of flash.
- On CompassTag, CompassTagAT25Breakout and CompassTagAT25, `.persistent` moves
  down one page and keeps its size, because the calibration page that used to
  sit between it and the code now sits above it. Internal log capacity is
  unchanged.
- `flash_release.py --keep-calibration` computes `-e [0 126]` for the
  CompassTag images and refuses images that store no calibration. **It has not
  yet been run on hardware.**

## Consequences

- Upgrades that keep the calibration format no longer cost a recalibration on
  CompassTag. They do cost the stored configuration and the logs, as they did
  before.
- A family that adds a `.calibration` table on the L432 must set
  `TAG_CALIBRATION_PAGES`, or the link fails.
- A change to the calibration record format still requires a mass erase,
  because the bytes stay at the same address under a different meaning.
