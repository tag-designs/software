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

### Shared math

- Promote any remaining CompassTag orientation helpers from SensorViz into
  `sensoranalysis` when they are algorithmic rather than presentational. (The
  calibration-constant conversions already live there:
  `CompassCalibration::fromMagnetometerJson()` and
  `fromMagnetometerConstants()`.)

### SensorViz handling of processed files

Plan this once the output database contract is stable. SensorViz should load
materialized processed streams through normal stream metadata. Open decisions:

- Should SensorViz prefer materialized orientation streams over live-derived
  `compass_heading` style streams?
- Should it show both raw/live-derived and processed streams, with labels that
  make provenance clear?
- Should SensorViz expose processing configuration from `ProcessingRun` in the
  File Info tab?
- Should SensorViz continue applying display heading settings to its live
  derived `compass_heading` stream while showing materialized yaw as ordinary
  processed data?

Until these are answered, DataProcessing keeps raw streams untouched and uses
stable processed stream ids that SensorViz can display as ordinary data.
