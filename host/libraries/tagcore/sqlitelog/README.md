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
packed samples rather than a decoded protobuf record list.

### Download unit

One ACK carries one **two-hour block**: up to 24 twelve-byte samples, each with
float pressure in hPa, float BMP585 temperature in degrees C, and five six-bit
activity counts covering the five minutes that follow the sample. The layout and
every decode rule live in `include/uiuctag_log_format.h`, shared verbatim with
the firmware; this decoder takes all geometry from those macros rather than
repeating constants.

The download index space is the internal checkpoint index, and one checkpoint
always describes exactly one external block, so there are no holes in it.

### Timing

`UIUCTagLog.epoch` is the time of the block's own slot 0, so slot times are a
plain offset from it with no rounding:

- sample `s` is at `epoch + s * 300` (`uiuctagSampleEpoch()`)
- activity bucket `b` of sample `s` starts at `+ b * 60`
  (`uiuctagActivityBucketEpoch()`)

Collection anchors its sample grid at the first minute boundary of the run and
each later block begins exactly one block period after the previous one, so a
header epoch is generally *not* a multiple of 7200. Do not round it down to a
two-hour boundary; that would shift every sample in the block.

The array index *is* the slot number: the firmware always sends a block from slot
0 and trims only trailing unwritten slots, so a short payload is a valid partial
block and never a shifted one. Because each block is anchored at its own first
sample, no two blocks can share a start time, and a block that ended early — at a
reset or a hibernation window — simply has unwritten slots at the end.

### Missing data

**NaN means no measurement.** Absent values are omitted from the tables rather
than stored as a placeholder, so gaps stay visible in plots and out of
aggregates. Two distinct causes both read as NaN:

- The slot was never written. Erased flash reads as `0xFFFFFFFF`, which is
  itself a quiet NaN.
- The conversion failed. The firmware stores a canonical quiet NaN.

`uiuctagSampleHasPressure()` and `uiuctagSampleHasTemperature()` test for any
NaN and therefore cover both. Note this differs deliberately from
`imutag.cc`, which compares against one exact NaN encoding — that test would
miss the erased-flash case, which is a normal occurrence in a UIUCTag log.

Activity has its own marker: `packed_activity_data == 0xFFFFFFFF` means the word
was never written. That is the expected state of the newest sample in any log,
because a sample's activity is programmed one sample period after its pressure.
Gaps of both kinds can appear anywhere in a block, not only at the tail.

### Rows

- `Voltage`: one row per block, at the raw checkpoint `epoch`, since the reading
  is taken as the block opens.
- `Pressure`, `Temperature`: one row per sample that has that value, at the
  sample's slot epoch.
- `Activity`: five rows per sample whose activity word was written, one per
  one-minute bucket, as a percentage — `active_seconds * 100 / 60`, matching the
  `%` units the `Activity` stream metadata declares and the convention the
  BitPresTag and CompassTag decoders use.

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
  against the one quiet-NaN encoding `0x7fc00000` (`isMissingAux()`), unlike
  the UIUCTag decoder above.
