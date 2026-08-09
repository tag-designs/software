# SQLite Log Schemas

This directory owns the project SQLite writer used by `tag-dwnld` and Qt
download paths. `schema.cc` declares the tables and stream metadata, while the
tag-specific `*.cc` files decode protobuf log pages into rows.

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
