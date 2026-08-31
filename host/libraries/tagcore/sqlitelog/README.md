# SQLite Log Schemas

This directory owns the project SQLite writer used by `tag-dwnld` and Qt
download paths. `schema.cc` declares the tables and stream metadata, while the
tag-specific `*.cc` files decode protobuf log pages into rows.

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
timestamps directly or rebuild their own timing model.

### Metadata

The generic `info` table contains JSON copies of the tag info and config.
For IMUTag clock reconstruction, the `info` table row with `fieldname='info'`
contains a JSON property named `ppm_clock_error`; this is the RV-3028 factory
correction in ppm. Missing `ppm_clock_error` means older firmware; the writer
assumes zero correction and emits a debug log message.

### `ImuHeader`

One row is written for each downloaded IMUTag page header.

- `HeaderIndex`: zero-based header/page counter in download order.
- `SegmentId`: reconstruction segment active at this header.
- `StartElapsedUs`: corrected elapsed microseconds assigned to this page start.
- `Epoch`: raw RTC epoch seconds from the page header.
- `Millisecond`: rounded millisecond decoded from the 1/1024-second subsecond
  ticks.
- `SubsecondTicks`: raw low-ten-bit subsecond tick field.
- `SubsecondHz`: tick frequency for `SubsecondTicks`, currently 1024 Hz.
- `Flags`: raw IMUTag header flags. Important bits are:
  - `0x0400`: `RESYNC`, start a new timing segment.
  - `0x0800`: `RESYNC_STORAGE_SKIP`, the previous segment ended with a storage
    skip.
  - `0x1000`: `RESTART_RECOVERY`, the segment follows monitor reset recovery.
- `Temperature`: pressure-sensor temperature in degrees C from the page header.

Ordinary page headers are diagnostic checkpoints. They are not used to adjust
the sample grid because rounded milliseconds can add page-to-page jitter.

### `ImuSegment`

One row is written for collection start and each explicit resync segment.

- `SegmentId`: monotonically increasing segment id.
- `HeaderIndex`: header that anchors this segment.
- `StartElapsedUs`, `Epoch`, `Millisecond`, `SubsecondTicks`, `SubsecondHz`,
  and `Flags`: anchor values copied from the corresponding `ImuHeader`.
- `Event`: decoded segment reason: `COLLECTION_START`, `RESYNC`,
  `RESYNC_STORAGE_SKIP`, or `RESTART_RECOVERY`.
- `FirstSampleIndex`: sample index within this segment at the anchor.
- `ConfiguredOdrHz`: IMU ODR used for nominal sample spacing.
- `CorrectionPpm`: ppm correction used to map nominal sample elapsed time to
  corrected elapsed time.

### Sample Tables

`ImuAccel`, `ImuGyro`, `ImuMag`, `ImuPressure`, and `ImuTemperature` share timing
columns:

- `SegmentId`: reconstruction segment containing the sample.
- `SampleIndex`: nominal sample index within that segment. For auxiliary rows
  this is the first IMU sample index in the superframe.
- `RawElapsedUs`: uncorrected elapsed microseconds from the segment start:
  `SampleIndex * nominal_timestep_us`.
- `ElapsedUs`: corrected elapsed microseconds used by sensorViz and ordinary
  exports:
  `segment_start + round(SampleIndex * nominal_timestep_us *
  (1 + CorrectionPpm / 1e6))`.

The remaining columns are sensor values in engineering units:

- `ImuAccel.ax/ay/az`: acceleration in mg.
- `ImuGyro.gx/gy/gz`: angular velocity in dps.
- `ImuMag.mx/my/mz`: magnetic field in uT.
- `ImuPressure.Pressure`: pressure in mbar.
- `ImuTemperature.Temperature`: sensor temperature in degrees C.

Kalman filters and other fusion tools should generally use `ElapsedUs` as the
best timestamp estimate, but keep `SegmentId`, `SampleIndex`, `RawElapsedUs`,
and `ImuSegment` available so clock state, restarts, and timing uncertainty can
be modeled explicitly.

### `ImuEvent`

`ImuEvent` stores vertical marker events for viewers:

- `StartElapsedUs`: corrected elapsed time of the event.
- `HeaderIndex`: header that produced the event.
- `Event`: decoded event name. `RESTART_RECOVERY` is a resync caused by monitor
  reset recovery, while `RESYNC_STORAGE_SKIP` indicates a storage discontinuity.
