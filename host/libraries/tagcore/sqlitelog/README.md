---
type: design
status: current
summary: How the SQLite log writer decodes UIUCTag and IMUTag downloads into rows, and a known BitPresTag activity-geometry inconsistency; the schema itself is in the user reference.
---

# SQLite Log Writer

This directory owns the project SQLite writer used by `tag-dwnld` and Qt
download paths. `schema.cc` declares the tables and stream metadata, while the
tag-specific `*.cc` files decode protobuf log pages into rows. To add a tag to
this path, start with the recipe comment at the top of `../sqlitelog.cc`.

The tables, columns and units a log contains are documented in
[SQLite Log Format](../../../docs/src/reference/sqlite-logs.md), which is the
authoritative schema. This document covers the decoder rules behind those rows
that a maintainer needs and a user does not.

## UIUCTag Downloader Fields

UIUCTag reuses the BitPresTag table and stream shape — `Voltage`, `Pressure`,
`Temperature`, `Activity` — so viewers and analysis queries are identical across
the two tags. Only the decoder differs: UIUCTag downloads a raw byte image of
its records rather than a decoded protobuf record list.

### Download unit

One ACK carries one **32-minute block**: up to 32 eight-byte records, each a raw
BMP585 pressure (unsigned 24-bit, hPa = raw / 6400) and temperature (signed
24-bit, degC = raw / 65536) reading, and three 5-bit counts of active seconds,
one per 20 s, for the minute before the reading. The layout, the conversions
and every decode rule live in `include/uiuctag_log_format.h`, shared verbatim
with the firmware; this decoder takes all geometry from those macros and
helpers rather than repeating constants. The design is in the
[UIUCTag overview](../../../../embedded/tags/UIUCTag/design/overview.md).

This is data layout 2 (firmware 2.0.0). The 12-byte five-minute layout 1 is not
decoded; no tag in that format was deployed
([decision 0044](../../../../docs/decisions/0044-uiuctag-minute-records.md)).
`txtlogs.cc` and the capture decoder in `../recovery/capturesource.cc` follow
the same rules; the text log writes activity as `epoch,ACT:percent`.

The download index space is the internal checkpoint index, and one checkpoint
always describes exactly one external block, so there are no holes in it.

### Timing

`UIUCTagLog.epoch` is the time of the block's own record 0, so record times are
a plain offset from it with no rounding:

- record `s` is at `epoch + s * 60` (`uiuctagRecordEpoch()`)
- activity bucket `b` of record `s` starts at `epoch + s * 60 - 60 + b * 20`
  (`uiuctagActivityBucketEpoch()`): the three buckets cover the minute that
  ends at the reading

Collection anchors its grid at the first minute boundary of the run and each
later block begins exactly one block period after the previous one, so a header
epoch is generally *not* a multiple of 1920. Do not round it; that would shift
every record in the block.

The array index *is* the slot number: the firmware always sends a block from
record 0 and trims only trailing unwritten records, so a short payload is a
valid partial block and never a shifted one. Because each block is anchored at
its own first record, no two blocks can share a start time, and a block that
ended early — at a reset or a hibernation window — simply has unwritten records
at the end. A payload that is not a whole number of records, or longer than
256 bytes, is rejected.

### Missing data

Absent values are omitted from the tables rather than stored as a placeholder,
so gaps stay visible in plots and out of aggregates:

- **Erased record**: activity word, the first two bytes, `0xFFFF`
  (`uiuctagRecordErased()`). A written activity word has bit 15 clear, so this
  test is exact. The record produces no rows.
- **Failed reading**: pressure and temperature both `0`
  (`UIUCTAG_RAW_FAILED`). No pressure or temperature row; the record's activity
  is still decoded (`uiuctagRecordHasReading()`).
- **Unobserved bucket**: value `0x1F`, written in all three buckets of a minute
  the tag did not watch end to end (the first record of a run, or after a reset,
  missed wake or hibernation). Counts 21-30 cannot be written and are treated
  the same way. `uiuctagActivityBucket()` returns
  `UIUCTAG_ACTIVITY_NOT_OBSERVED` for both, and no row is written.

Every record the firmware writes carries its activity word, so unlike the old
format the newest record of a log is complete. Gaps can appear anywhere in a
block, not only at the tail.

### Rows

- `Voltage`: one row per block, at the raw checkpoint `epoch`, since the reading
  is taken as the block opens.
- `Pressure`, `Temperature`: one row each per record with a reading, at the
  record's epoch.
- `Activity`: up to three rows per record, one per observed 20-second bucket,
  at the bucket's start, as a percentage — `active_seconds * 100 / 20`,
  matching the `%` units the `Activity` stream metadata declares and the
  convention the BitPresTag and CompassTag decoders use.

## Known Inconsistency: BitPresTag Activity Geometry

The two BitPresTag decoders disagree about how `BitPresTagLog.activity` is
packed, and both hard-code it:

- `sqlitelog/pressure.cc` reads 4 buckets of 4 bits over a 15-second period.
- `txtlogs.cc` reads 5 buckets of 6 bits over a 60-second period.

The 15-second form matches the debug constants currently compiled into
`families/BitPresTag/src/state_run.c`; the 60-second form matches what those
constants are commented as being in production. At most one decoder is right for
any given firmware image, so a BitPresTag activity series should be treated as
suspect until this is resolved against a known capture.

Not fixed here because it needs a decision about which firmware geometry is
authoritative, and a check of whether existing logs were captured with the debug
constants. UIUCTag avoids the whole class of problem by taking its geometry from
`include/uiuctag_log_format.h`, which the firmware includes too.

## IMUTag Downloader Fields

IMUTag logs have two time domains:

- RTC page headers are wall-clock anchors produced by the tag.
- IMU, pressure, temperature, and magnetometer rows are stored on a reconstructed
  elapsed-time sample grid.

The downloader preserves both pieces so analysis tools can use the corrected
timestamps directly or rebuild their own timing model. The table and column
definitions (`ImuHeader`, `ImuSegment`, `ImuEvent`, the sample tables and their
timing columns, and the `ElapsedUs` formula) are in
[SQLite Log Format](../../../docs/src/reference/sqlite-logs.md#imutag).

Writer rules:

- The clock correction comes from the `ppm_clock_error` property in the `info`
  table row with `fieldname='info'`; this is the RV-3028 factory correction in
  ppm. Missing `ppm_clock_error` means older firmware; the writer assumes zero
  correction and emits a debug log message. The value used is copied into
  `ImuSegment.CorrectionPpm`.
- Only collection start and explicit resync headers (`RESYNC`,
  `RESYNC_STORAGE_SKIP`, `RESTART_RECOVERY` flags) anchor a segment. Ordinary
  page headers are diagnostic checkpoints and do not adjust the sample grid,
  because rounded milliseconds can add page-to-page jitter.
- Missing auxiliary values (pressure, magnetometer) are detected by comparing
  against the one quiet-NaN encoding `0x7fc00000` (`isMissingAux()`).
