---
type: worklist
status: current
summary: Open command-line tool work - dataprocessing tests, further processors and SensorViz support for processed files.
---

# Command-Line Tools TODO

## dataprocessing

Design: [design/dataprocessing.md](design/dataprocessing.md).

### Tests

There are none yet. Unit tests:

- Compass calibration application matches existing SensorViz/qtcalibrate
  expectations.
- Compass orientation output is deterministic for a small fixture.
- Downstream heading conversion examples match SensorViz and qtcalibrate
  display conventions.
- Missing calibration and malformed record sets fail with actionable errors.

Fixture tests:

- Use `host/docs/fixtures/sensorviz/compasstag.db3` as an initial CompassTag
  input.
- Confirm the input file hash does not change.
- Confirm output SQLite integrity check passes.
- Confirm output stream metadata references existing output tables and columns.
- Confirm rerun behavior for `--if-exists fail`, `replace`, and `keep`.

Manual review, once SensorViz support exists:

- Load the processed output in SensorViz.
- Compare processed orientation streams against current SensorViz live-derived
  streams for the same fixture.

### Further processors

| Processor | Inputs | Outputs |
| --- | --- | --- |
| `altitude` | pressure stream, optional sensor temperature | altitude stream |
| `activity-filter` | activity stream | filtered activity stream |
| `imu-magnitudes` | IMUTag accelerometer/gyroscope/magnetometer axes | magnitude streams |
| `imu-kalman-attitude` | IMU accel/gyro/mag streams, timing metadata, filter config | attitude/orientation streams and filter diagnostics |

### IMUTag orientation post-processing

**Blocked on real data.** Filter design waits for logs collected on the
prototype tags now at fab; synthetic logs cannot show real motion, mag timing
or disturbance.

This needs a design before more processors are added. A download is the whole
deployment, so orientation can be estimated offline with a forward EKF over
the 400 Hz accel and gyro, corrected by the 40 Hz magnetometer, and a backward
(RTS) smoothing pass. That would replace `imu-compass-orientation`'s
single-sample eCompass.

**Rule until then (2026-10-21):**

- SensorViz computes orientation live only from a true eCompass record set:
  CompassTag `compass_raw`, where each row is a simultaneous accelerometer and
  magnetometer reading.
- SensorViz never pairs IMUTag `ImuAccel` with `ImuMag` itself. It shows
  IMUTag orientation only from a precomputed `imu-compass-orientation`
  output.
- That processor stays as it is.

Findings from 2026-10-21, measured on a synthetic full 2 Gbit log of 19.66M IMU
and 1.97M mag samples:

- **The superframe mean is a decimation step, not a gravity estimate.** A
  10-sample boxcar at 400 Hz is -3 dB near 18 Hz, so 0.5-10 Hz animal motion
  passes almost unchanged. 50 mg of dynamic acceleration tilts "down" by about
  3 degrees, roughly 6 degrees of heading at 65 degrees of dip.
  [design/dataprocessing.md](design/dataprocessing.md#pairing-imutag-samples)
  overstates the mean as suppressing motion.
- **Magnetometer timing is not logged.** The BMM350 free-runs at 50 Hz and is
  read after each IMU block (`embedded/tags/families/IMUTag/src/sensors.c`). Its
  sample age therefore varies by up to 20 ms against the 40 Hz superframe. A
  filter needs a model of that age, or firmware that logs it.
- **Costs.**
  - `solveOrientation()`: 134 ns per pair.
  - Reading a stored `ImuCompassOrientation` row: 136 ns. The table adds
    170 MB.
  - Reading raw rows: `ImuMag` 82 ns/row, `ImuAccel` 122 ns/row (2.4 s for the
    full log).
  - A full-rate filter must read both 19.7M-row tables, about 4.8 s, and run
    sequentially.
  - So a stored, provenance-tagged filter output is worth keeping, even though
    a stored single-sample eCompass result is not.

Open decisions:

- Filter: forward EKF plus RTS smoother, or a complementary filter. Choose the
  state (attitude, gyro bias, possibly mag disturbance) and how its
  configuration is recorded in `ProcessingRun`.
- Mag timing: model the unknown sample age, or have firmware log it.
- Whether the writer should store a superframe-mean accel beside each `ImuMag`
  row. This is on hold; it mainly serves the single-sample method and a 40 Hz
  overview stream.
- Where the filter lives: `sensoranalysis` core, Qt-free, with the same
  bit-compatibility rules as the compass core.
- What SensorViz shows: stored filter output only, or also a live
  single-sample eCompass for comparison. See
  [SensorViz handling of processed files](#sensorviz-handling-of-processed-files).
- Heading conversion (declination, mounting) stays downstream, per
  [decision 0030](../../docs/decisions/0030-host-orientation-stored-as-quaternion-only.md).

### Shared math

- Promote any remaining CompassTag orientation helpers from SensorViz into
  `sensoranalysis` when they are algorithmic rather than presentational. (The
  calibration-constant conversions already live there:
  `CompassCalibration::fromMagnetometerJson()` and
  `fromMagnetometerConstants()`.)

### SensorViz handling of processed files

Plan this once the output database contract is stable. SensorViz should load
materialized processed streams through normal stream metadata. Open decisions:

- SensorViz now prefers a processed file's stored orientation quaternions over
  re-deriving them from raw CompassTag rows. Should it offer the live
  derivation as well, for comparing a processed file against current code?
- Should it show both raw/live-derived and processed streams, with labels that
  make provenance clear?
- Should SensorViz expose processing configuration from `ProcessingRun` in the
  File Info tab?

Until these are answered, DataProcessing keeps raw streams untouched and uses
stable processed stream ids that SensorViz can display as ordinary data.
