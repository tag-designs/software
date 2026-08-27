# DataProcessing Post-Processing Application Design

## Purpose

SensorViz can derive calibrated compass streams and orientation values while a
log is being viewed, but those results are session-local. Custom notebooks,
batch analysis scripts, and downstream tools still see only the raw SQLite log.

`DataProcessing` is a proposed host command-line application that materializes
selected derived data into a new SQLite log. The first processor should read
CompassTag raw samples and calibration constants, then write calibrated compass
data and compass orientation streams. Later processors can add altitude,
filtered activity, corrected IMU timing products, Kalman-filtered IMU attitude,
or other analysis outputs.

The application boundary is:

- SensorViz visualizes raw and derived streams.
- qtcalibrate collects calibration samples and produces calibration constants.
- DataProcessing writes durable derived streams for analysis outside the GUI.

## Goals

- Keep the input SQLite log untouched.
- Generate a new SQLite output file containing original data plus selected
  derived tables and stream metadata.
- Embed processing configuration, algorithm versions, and provenance in the
  output database.
- Reuse existing CompassTag math instead of duplicating it across SensorViz,
  qtcalibrate, and DataProcessing.
- Make generated data discoverable through the same stream metadata model used
  by raw tag logs.
- Support deterministic batch operation from scripts and CI fixtures.

## Non-Goals

- Do not change the raw log writer in the first implementation.
- Do not make SensorViz depend on preprocessed files before the output format is
  proven.
- Do not overwrite the input file in place.
- Do not turn viewer preferences such as plot colors, axis sides, or visibility
  into processing configuration.
- Do not make the first version a Qt GUI. A command-line tool is easier to test
  and compose with external analysis workflows.

## Existing Components

### SensorViz

SensorViz currently loads SQLite stream metadata, turns CompassTag raw record
sets into display streams, and applies view-only choices such as declination
and battery-forward direction when drawing heading. This gives an important
prototype for stream names, units, and user expectations, but SensorViz should
not remain the only place where derived data can exist.

The CompassTag display path already uses `host/libraries/sensoranalysis` for
the core eCompass solve. That is the right direction: UI code should assemble
loaded rows and presentation choices, while `sensoranalysis` owns the
UI-independent math.

### qtcalibrate

qtcalibrate owns the live magnetic calibration workflow. It collects calibration
samples, runs the inherited C `magcal` solver, shows quality metrics, and writes
calibration constants back to the tag. It also shows live orientation and now
uses `sensoranalysis::CompassProcessor` for the orientation solve after
qtcalibrate has applied calibration and low-pass filtering.

This matters for refactoring: DataProcessing should not fork compass
orientation math from SensorViz, and it should not fork calibration-constant
conversion from qtcalibrate. The shared layer should cover:

- conversion between `magcal` constants and `CompassCalibration`;
- applying hard-iron and soft-iron calibration to raw magnetometer vectors;
- deriving magnetic-frame orientation from accelerometer and magnetometer data.

The live calibration solver can remain in qtcalibrate initially, but any
non-UI adapter code needed by both qtcalibrate and DataProcessing should move
toward `sensoranalysis`.

### sensoranalysis

`host/libraries/sensoranalysis` already contains:

- `CompassCalibration`
- `CompassRawSample`
- `CompassDerivedSample`
- `CompassProcessor`

The library should remain independent of widgets, QML, plotting, and SQLite.
DataProcessing can call this library directly, while SQLite input/output stays
in the new application or in a small host data-processing support library.

## Application Shape

The executable should be named `dataprocessing`.

Typical usage:

```sh
dataprocessing \
  --input raw-compass.db3 \
  --output processed-compass.db3 \
  --processor compass-calibrated \
  --processor compass-orientation
```

Useful inspection modes:

```sh
dataprocessing --input log.db3 --list-processors
dataprocessing --input log.db3 --describe compass-orientation
dataprocessing --input log.db3 --dry-run --processor compass-orientation
```

Output policy:

```sh
dataprocessing --input raw.db3 --output processed.db3 --if-exists fail
dataprocessing --input raw.db3 --output processed.db3 --if-exists replace
dataprocessing --input raw.db3 --output processed.db3 --if-exists keep
```

Default `--if-exists` should be `fail` for early versions so reruns do not
silently hide stale processing decisions.

## Processing Model

Each processor should declare:

- processor id and algorithm version;
- required source tables, streams, metadata, and calibration records;
- configuration schema and defaults;
- output tables and output stream ids;
- replacement policy support;
- validation checks for row counts, column presence, units, and time domains.

Initial processors:

| Processor | Inputs | Outputs |
| --- | --- | --- |
| `compass-calibrated` | `Compass` table, latest `Calibration` row | calibrated magnetometer x/y/z record set, optional calibrated acceleration copy |
| `compass-orientation` | `Compass` table, latest `Calibration` row | magnetic-frame yaw, pitch, roll, dip, field strength, acceleration magnitude, and quaternion |

Future processors:

| Processor | Inputs | Outputs |
| --- | --- | --- |
| `altitude` | pressure stream, optional sensor temperature | altitude stream |
| `activity-filter` | activity stream | filtered activity stream |
| `imu-magnitudes` | IMUTag accelerometer/gyroscope/magnetometer axes | magnitude streams |
| `imu-kalman-attitude` | IMU accel/gyro/mag streams, timing metadata, filter config | attitude/orientation streams and filter diagnostics |

## First CompassTag Output

The first implementation should copy the input database to the output path,
open the output read/write, then add derived tables.

Recommended tables:

```text
CompassCalibrated
  Epoch INTEGER
  ax REAL
  ay REAL
  az REAL
  mx REAL
  my REAL
  mz REAL

CompassOrientation
  Epoch INTEGER
  yaw REAL
  pitch REAL
  roll REAL
  dip REAL
  field REAL
  acceleration REAL
  qw REAL
  qx REAL
  qy REAL
  qz REAL
```

`CompassCalibrated` should contain magnetometer values after hard-iron and
soft-iron correction. Acceleration values may be copied from the raw table so
the calibrated record set is self-contained, but the table description must
make clear that acceleration was not magnetometer-calibrated.

`CompassOrientation` should contain canonical magnetic-frame orientation, not a
display heading. Declination and battery-forward/backward mounting conventions
belong to downstream interpretation, SensorViz display state, or qtcalibrate
display state. DataProcessing should document the conversion formula but should
not materialize a heading column in the first implementation.

Recommended stream ids:

```text
compass_calibrated_ax
compass_calibrated_ay
compass_calibrated_az
compass_calibrated_mx
compass_calibrated_my
compass_calibrated_mz
compass_orientation_yaw
compass_orientation_pitch
compass_orientation_roll
compass_orientation_dip
compass_orientation_field
compass_orientation_acceleration
```

The stream metadata should use the existing `streams` catalog style:

- scalar streams for orientation values;
- `record_column` entries for grouped calibrated vectors;
- stable ids that do not collide with SensorViz live-derived display stream ids
  unless the project intentionally decides to treat the materialized streams as
  replacements.

## Embedded Processing Metadata

Every run should write provenance into the output database. A normalized table
plus JSON configuration gives both queryable history and extensibility.

Recommended table:

```text
ProcessingRun
  RunId INTEGER PRIMARY KEY
  ToolName TEXT
  ToolVersion TEXT
  ProcessorId TEXT
  ProcessorVersion INTEGER
  CreatedUtc TEXT
  InputFileName TEXT
  InputSha256 TEXT
  ConfigurationJson TEXT
  SourceTablesJson TEXT
  OutputTablesJson TEXT
  Status TEXT
```

Example configuration for compass orientation:

```json
{
  "processor": "compass-orientation",
  "algorithm_version": 1,
  "calibration_source": {
    "table": "Calibration",
    "epoch": 1778983834
  },
  "orientation_frame": "magnetic-frame-nwu",
  "quaternion_order": "wxyz"
}
```

Processing configuration is not a SensorViz preference. It describes how the
data was computed and must travel with the output database.

## Downstream Heading Conversion

DataProcessing should document, but not perform, the conversion from canonical
magnetic yaw to a display heading. Downstream tools that need a user-facing
heading can apply:

```text
magnetic_heading = normalize_360(yaw + declination_degrees)
display_heading = battery_forward
  ? magnetic_heading
  : normalize_360(magnetic_heading + 180.0)
```

Where:

- `yaw` is the materialized magnetic-frame yaw in degrees;
- `declination_degrees` is the downstream tool's chosen east-positive
  declination correction;
- `battery_forward` is the downstream tool's selected physical mounting
  convention;
- `normalize_360(x)` maps any angle to `[0, 360)`.

This keeps the processed SQLite output reusable. A downstream notebook can
choose local declination or mounting assumptions without rerunning calibration
and orientation processing.

## Refactoring Plan

1. Keep the `magcal` solver in qtcalibrate for now.
2. Move or add UI-free conversion helpers in `sensoranalysis` for all
   calibration constants that must be shared by qtcalibrate, SensorViz, and
   DataProcessing.
3. Promote any remaining CompassTag orientation helpers from SensorViz into
   `sensoranalysis` when they are algorithmic rather than presentational.
4. Keep SQLite read/write helpers outside `sensoranalysis` so that library
   remains independent of database format and application policy.
5. Add DataProcessing as a host command-line application that links
   `sensoranalysis`, SQLite, and the appropriate host common utilities.
6. Update SensorViz later to distinguish raw streams, materialized processed
   streams, and live display-derived streams.

The important ownership rule is:

```text
sensoranalysis: math and calibration/orientation data types
qtcalibrate: live sample collection and calibration solving UI
DataProcessing: batch SQLite copy, processor configuration, durable outputs
SensorViz: visualization and interactive display choices
```

## SensorViz Handling Of Augmented Files

SensorViz does not need to change in the first DataProcessing pass. Later, it
should load materialized processed streams through normal stream metadata.

Open decisions for that later work:

- Should SensorViz prefer materialized orientation streams over live-derived
  `compass_heading` style streams?
- Should it show both raw/live-derived and processed streams, with labels that
  make provenance clear?
- Should SensorViz expose processing configuration from `ProcessingRun` in the
  File Info tab?
- Should SensorViz continue applying display heading settings to its live
  derived `compass_heading` stream while showing materialized yaw as ordinary
  processed data?

Until those questions are answered, DataProcessing should avoid mutating or
removing raw streams and should use stable processed stream ids that SensorViz
can display as ordinary data.

## Validation Strategy

Unit tests:

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

Manual review:

- Load the processed output in SensorViz once augmented-file support is added.
- Compare processed orientation streams against current SensorViz live-derived
  streams for the same fixture.

## Implementation Phases

1. Create `dataprocessing` CLI skeleton with argument parsing, input/output
   path validation, database copy, and `--dry-run`.
2. Add `ProcessingRun` table creation and provenance writing.
3. Implement CompassTag input discovery and validation.
4. Implement `compass-calibrated`.
5. Implement `compass-orientation`.
6. Add tests against the CompassTag fixture and SQLite integrity checks.
7. Document maintainer usage and processor configuration.
8. Plan SensorViz augmented-file behavior after the output database contract is
   stable.
