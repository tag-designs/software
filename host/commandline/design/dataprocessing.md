---
type: design
status: current
summary: How the dataprocessing CLI copies a SQLite log and adds calibrated CompassTag streams and IMUTag orientation with provenance, and how the work is split with sensoranalysis, qtcalibrate and SensorViz.
---

# DataProcessing Post-Processing Application Design

`dataprocessing` (`host/commandline/dataprocessing.cc`) copies a SQLite log to
a new file and adds durable derived tables, stream metadata, and a
`ProcessingRun` provenance row to the copy. It has three processors: two for
CompassTag, `compass-calibrated` and `compass-orientation`, and one for IMUTag,
`imu-compass-orientation`. The input file is never modified. Usage is in the [command-line README](../README.md#dataprocessing);
open work is in [TODO.md](../TODO.md).

## Why It Exists

SensorViz derives calibrated compass streams and orientation while a log is
being viewed, but those results are session-local. Notebooks, batch scripts and
downstream tools see only the raw SQLite log. `dataprocessing` materializes the
derived data so it travels with the file.

The application boundary is:

```text
sensoranalysis: math and calibration/orientation data types
qtcalibrate: live sample collection and calibration solving UI
DataProcessing: batch SQLite copy, processor configuration, durable outputs
SensorViz: visualization and interactive display choices
```

`dataprocessing` links `sensoranalysis` and SQLite. It is built on Qt Core
(`QCoreApplication`, `QCommandLineParser`, Qt JSON and file classes) and gets
Qt Core and Qt Gui through `sensoranalysis`, so it installs as a Qt-runtime
target, but it has no GUI: it is scriptable and deterministic. It uses
`CompassCalibration::fromMagnetometerJson()` and `CompassProcessor` from
`sensoranalysis`, the same math SensorViz and qtcalibrate use, so compass math
is not forked between the three. SQLite read/write stays in the application,
keeping `sensoranalysis` independent of the database format. The `magcal`
calibration solver stays in qtcalibrate.

Viewer preferences (plot colors, axis sides, visibility) are never processing
configuration.

## Command Line

| Option | Behavior |
| --- | --- |
| `-i`, `--input <path>` | Input SQLite log. Must differ from the output path. |
| `-o`, `--output <path>` | Output SQLite log. |
| `-p`, `--processor <id>` | Processor to run; repeat for several. |
| `--if-exists fail\|replace\|keep` | Conflict policy, default `fail`. |
| `--dry-run` | Validate inputs and print planned outputs without writing. |
| `--list-processors` | List processors and whether the input log has what each needs. |
| `--describe <id>` | Describe one processor. |
| `--print-summary` | Print a processing summary. |

An unknown `--if-exists` value exits with status 2.

`--if-exists` applies twice. For the output file: `fail` refuses an existing
file, `replace` deletes and re-copies it, and `keep` reuses it. For each
processor's output tables and stream ids inside the file: `fail` refuses when
any already exist, `keep` skips that processor, and `replace` drops the tables
and stream rows before rewriting them. The default is `fail` so a rerun cannot
silently hide a stale processing decision.

## Processing Model

The input is copied byte for byte, the copy is opened read/write, and all
processor writes happen inside one `BEGIN IMMEDIATE` transaction that is
committed at the end.

| Processor | Inputs | Outputs |
| --- | --- | --- |
| `compass-calibrated` | `Compass` table, latest `Calibration` row | `CompassCalibrated` table and six `record_column` streams |
| `compass-orientation` | `Compass` table, latest `Calibration` row | `CompassOrientation` table and six scalar streams |
| `imu-compass-orientation` | `ImuMag` and `ImuAccel` tables, latest `Calibration` row | `ImuCompassOrientation` table and six scalar streams |

The calibration used is the `Calibration` row with the greatest `Epoch`; its
`Constants` JSON must contain a `magnetometer` object. Every processor applies
it through `CompassProcessor`: the magnetometer constants, and the
accelerometer zero-g offset when present.

Inputs are loaded per tag family, and only for the processors requested, so an
IMUTag log is not rejected for lacking a `Compass` table.

### Pairing IMUTag samples

IMUTag logs the magnetometer once per superframe of
`IMUTAG_IMU_SAMPLES_PER_SUPERFRAME` (10) IMU samples, so there is no one-to-one
accelerometer row to pair it with. An `ImuMag` row's `SampleIndex` is the first
IMU sample index of its superframe, and `imu-compass-orientation` pairs it with
the mean of the `ImuAccel` rows in the same segment whose `SampleIndex` lies in
`[SampleIndex, SampleIndex + 10)`. The mean covers the span the magnetometer
value stands for and suppresses motion in the gravity estimate. Its centre is
about half a superframe after the row's `ElapsedUs`, which is the time the
output is written at.

A magnetometer row with no accelerometer rows in its superframe, or a pair the
solver cannot use (a zero vector), is counted and skipped rather than failing
the run; `--dry-run` and `--print-summary` report the counts. The output's
`AccelSamples` column records how many rows each mean used.

The result is only as good as the tag's axis alignment; see
[Magnetometer and Accelerometer Axes](../../../docs/shared/sensor-axes.md).
Logs from firmware without its board's magnetometer mapping give orientation
with a dip that wanders with attitude.

### Output Tables

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

ImuCompassOrientation
  SegmentId INTEGER
  SampleIndex INTEGER
  ElapsedUs INTEGER
  yaw REAL
  pitch REAL
  roll REAL
  dip REAL
  field REAL
  acceleration REAL
  AccelSamples INTEGER
  qw REAL
  qx REAL
  qy REAL
  qz REAL
```

`CompassCalibrated` holds magnetometer values after hard-iron and soft-iron
correction, beside acceleration with the zero-g offset removed, so the record
set is self-contained.

`ImuCompassOrientation` carries the `ImuMag` row's `SegmentId`, `SampleIndex`
and `ElapsedUs`, so it lines up with the raw IMUTag tables, and the same
orientation columns as `CompassOrientation`. `acceleration` is the magnitude of
the superframe mean.

`CompassOrientation` holds canonical magnetic-frame orientation, not a display
heading. Declination and battery-forward/backward mounting belong to
downstream interpretation or to SensorViz and qtcalibrate display state, so no
heading column is written (see [Downstream Heading Conversion](#downstream-heading-conversion)).

### Stream Ids

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
imu_compass_orientation_yaw
imu_compass_orientation_pitch
imu_compass_orientation_roll
imu_compass_orientation_dip
imu_compass_orientation_field
imu_compass_orientation_acceleration
```

They use the existing `streams` catalog style: `record_column` entries for the
grouped calibrated vectors and scalar streams for orientation values. The IMUTag
streams use `ElapsedUs` as their time column, like the raw IMUTag streams. The ids
do not collide with SensorViz's live-derived display stream ids. The quaternion
columns have no stream entries. Raw streams are never mutated or removed.

## Provenance

Every successful processor run inserts one row into `ProcessingRun`, created if
absent:

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

`ConfigurationJson` records the processor, `algorithm_version` (currently 1)
and the calibration source. For both orientation processors it also records the
frame, quaternion order and heading policy, and for `imu-compass-orientation`
an `accel_pairing` object naming the method (`superframe_mean`) and the
superframe size. An illustrative example (the epoch is a
placeholder):

```json
{
  "processor": "compass-orientation",
  "algorithm_version": 1,
  "calibration_source": {
    "table": "Calibration",
    "epoch": 1778983834
  },
  "orientation_frame": "magnetic-frame-nwu",
  "quaternion_order": "wxyz",
  "heading_policy": "not_materialized; downstream tools apply declination and mounting convention to yaw"
}
```

Processing configuration is not a SensorViz preference. It describes how the
data was computed and travels with the output database.

## Downstream Heading Conversion

Downstream tools that need a user-facing heading apply:

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

This keeps the processed output reusable: a notebook can choose local
declination or mounting assumptions without rerunning calibration and
orientation processing.

## SensorViz And Processed Files

SensorViz has no support specific to processed files. It does not read
`ProcessingRun` and does not distinguish materialized streams from raw or
live-derived ones. Because it loads every `scalar` row in `streams`, the
orientation streams of a processed file plot beside the raw ones; for an IMUTag
log that is the only place orientation appears, since SensorViz derives none
for IMUTag itself. The open questions for adding that support are in
[TODO.md](../TODO.md).

## Verification

There are no automated tests for `dataprocessing`. The checks to make by hand
or in a future test are listed in [TODO.md](../TODO.md).

The original step-by-step implementation plan is kept, as history, in
[proposals/dataprocessing-implementation-plan.md](proposals/dataprocessing-implementation-plan.md).
