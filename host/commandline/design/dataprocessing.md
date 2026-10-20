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

`dataprocessing` uses no Qt. It links `sensoranalysis_core` -- the Qt-free
compass core -- through `sensoranalysis_core_json`, plus SQLite, and borrows
two self-contained files from tagcore: the vendored cxxopts header for its
command line and `Sha256` for the input hash. It is a plain executable,
scriptable and deterministic, built in any host build whether or not the Qt
applications are
([decision 0031](../../../docs/decisions/0031-host-dataprocessing-plain-executable-without-qt.md)).

The compass math is not forked. `compasscore::calibrationFromJson()` reads the
newest Calibration row and `compasscore::solveOrientation()` solves each pair;
SensorViz and qtcalibrate reach the same functions through
`CompassCalibration` and `CompassProcessor`, which are adapters over the core.
The core is bit-identical to the Qt code it replaced, which
`compass_core_parity_check` verifies, so a log processed here holds exactly
what SensorViz derives from the same rows. SQLite read/write stays in the
application, keeping `sensoranalysis` independent of the database format. The
`magcal` calibration solver stays in qtcalibrate.

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
| `compass-orientation` | `Compass` table, latest `Calibration` row | `CompassOrientation` table and its `compass_orientation` record set |
| `imu-compass-orientation` | `ImuMag` and `ImuAccel` tables, latest `Calibration` row | `ImuCompassOrientation` table and its `imu_compass_orientation` record set |

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

`CompassOrientation` holds magnetic-frame orientation as a quaternion
(`qw, qx, qy, qz`), not Euler angles and not a display heading. Euler angles
are a convention -- a rotation order and an axis labelling -- and libraries
disagree on it, so stored angles would tie the data to whichever library
computed them. Heading, pitch and roll, declination and battery-forward/backward
mounting all belong to downstream interpretation or to SensorViz and qtcalibrate
display state (see [Downstream Heading Conversion](#downstream-heading-conversion)
and [decision 0030](../../../docs/decisions/0030-host-orientation-stored-as-quaternion-only.md)).

### Stream Ids

```text
compass_calibrated_ax
compass_calibrated_ay
compass_calibrated_az
compass_calibrated_mx
compass_calibrated_my
compass_calibrated_mz
compass_orientation_qw
compass_orientation_qx
compass_orientation_qy
compass_orientation_qz
compass_orientation_dip
compass_orientation_field
compass_orientation_acceleration
imu_compass_orientation_qw
imu_compass_orientation_qx
imu_compass_orientation_qy
imu_compass_orientation_qz
imu_compass_orientation_dip
imu_compass_orientation_field
imu_compass_orientation_acceleration
```

They use the existing `streams` catalog style, all as `record_column` entries:
the calibrated vectors in the `compass_calibrated` group, and each orientation
table's columns, quaternion included, in a group named after the table's
processor (`compass_orientation`, `imu_compass_orientation`). Orientation is a
record set rather than scalar streams because its quaternion is only meaningful
as four columns together; the consumer turns it into the angles it displays.
The IMUTag streams use `ElapsedUs` as their time column, like the raw IMUTag
streams. The ids do not collide with SensorViz's display stream ids. Raw
streams are never mutated or removed.

Processor version 1 also wrote `yaw`, `pitch` and `roll` columns, and
registered yaw, pitch, roll, dip, field and acceleration as `scalar` streams.
Version 2 still removes the retired yaw, pitch and roll ids when it replaces an
older output, so a reprocessed file never lists a stream whose column is gone.

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

`ConfigurationJson` records the processor, `algorithm_version` (currently 2)
and the calibration source. For both orientation processors it also records the
frame, quaternion order and heading policy, and for `imu-compass-orientation`
an `accel_pairing` object naming the method (`superframe_mean`) and the
superframe size. An illustrative example (the epoch is a
placeholder):

```json
{
  "processor": "compass-orientation",
  "algorithm_version": 2,
  "calibration_source": {
    "table": "Calibration",
    "epoch": 1778983834
  },
  "orientation_frame": "magnetic-frame-nwu",
  "quaternion_order": "wxyz",
  "heading_policy": "not_materialized; downstream tools derive heading from the quaternion, then apply declination and mounting convention"
}
```

Processing configuration is not a SensorViz preference. It describes how the
data was computed and travels with the output database.

## Downstream Heading Conversion

The host displays use one Euler convention: Qt's `toEulerAngles()`, applied
where the angles are shown -- by `compassAnglesFromQuaternion()` in
`host/libraries/sensorui/compass_angles.h` for C++ displays, and by
`MyCompass.qml` on the quaternion it is given. A tool without Qt reproduces it
from the stored quaternion, normalized, as:

```text
pitch = asin(2 (qw qx - qy qz))
roll  = atan2(2 (qx qz + qw qy), 1 - 2 (qx^2 + qy^2))
z     = atan2(2 (qx qy + qw qz), 1 - 2 (qx^2 + qz^2))
yaw   = normalize_360(360 - z)
```

That is Qt's decomposition `q = Ry * Rx * Rz`, with the x angle shown as pitch,
the y angle as roll, and the z angle, reversed to read clockwise, as magnetic
yaw. It is not the aerospace Z-Y-X order, and the two give different yaw when
the tag is tilted about both horizontal axes. Near `|pitch| = 90` Qt switches
to a gimbal-lock branch that these formulas do not reproduce. Checked against
Qt on 1011 CompassTag and IMUTag rows, they agree to 3e-4 degrees.

Tools that need a user-facing heading then apply:

```text
magnetic_heading = normalize_360(yaw + declination_degrees)
display_heading = battery_forward
  ? magnetic_heading
  : normalize_360(magnetic_heading + 180.0)
```

Where:

- `yaw` is the magnetic-frame yaw above, in degrees;
- `declination_degrees` is the downstream tool's chosen east-positive
  declination correction;
- `battery_forward` is the downstream tool's selected physical mounting
  convention;
- `normalize_360(x)` maps any angle to `[0, 360)`.

This keeps the processed output reusable: a notebook can choose local
declination or mounting assumptions without rerunning calibration and
orientation processing.

## SensorViz And Processed Files

SensorViz builds every compass view from quaternions. For a processed file it
reads the `compass_orientation` or `imu_compass_orientation` record set and
uses the stored quaternions, ahead of re-deriving orientation from a
CompassTag's raw `Compass` table; for an unprocessed CompassTag log it derives
the quaternions itself with `CompassProcessor`. Heading, pitch and roll are
then computed from the quaternion in either case, so an IMUTag log -- for which
SensorViz derives no orientation itself -- gets the same compass streams and
panel once processed. File Info names the source. SensorViz does not read
`ProcessingRun`. The remaining open questions are in [TODO.md](../TODO.md).

## Verification

There are no automated tests for `dataprocessing`. The checks to make by hand
or in a future test are listed in [TODO.md](../TODO.md).

The original step-by-step implementation plan is kept, as history, in
[proposals/dataprocessing-implementation-plan.md](proposals/dataprocessing-implementation-plan.md).
