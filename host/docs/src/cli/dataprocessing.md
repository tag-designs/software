# dataprocessing

`dataprocessing` copies a downloaded SQLite log and adds computed sensor
streams to the copy. The original database is left unchanged so the downloaded
log remains the record of what came from the tag.

The processors support CompassTag and IMUTag logs:

| Processor | Tag | Added table | Function |
| --- | --- | --- | --- |
| `compass-calibrated` | CompassTag | `CompassCalibrated` | Copies accelerometer samples and writes calibrated magnetometer x/y/z values. |
| `compass-orientation` | CompassTag | `CompassOrientation` | Writes the orientation quaternion (`qw`, `qx`, `qy`, `qz`), dip, field strength, and acceleration magnitude columns. |
| `imu-compass-orientation` | IMUTag | `ImuCompassOrientation` | Writes the same orientation columns, one row per magnetometer sample. |

## Usage

```sh
dataprocessing \
  --input input.db3 \
  --output processed.db3 \
  --processor compass-calibrated \
  --processor compass-orientation
```

## Options

| Option | Value | Function |
| --- | --- | --- |
| `-i`, `--input` | SQLite file | Source log to read. |
| `-o`, `--output` | SQLite file | Processed copy to create or update. |
| `-p`, `--processor` | Processor id | Processor to run. Repeat this option to run more than one processor. |
| `--if-exists` | `fail`, `replace`, `keep` | Controls what happens when the output path already exists. The default is `fail`. |
| `--list-processors` | none | Prints available processors and whether the selected log contains their required inputs. |
| `--describe` | Processor id | Prints a short description of one processor. |
| `--dry-run` | none | Checks inputs and reports planned tables without writing output data. |
| `--print-summary` | none | Prints a short completion summary. |
| `-h`, `--help` | none | Prints command usage and exits. |

## Examples

Check which processors can run on a log:

```sh
dataprocessing --input compasstag.db3 --list-processors
```

Create a processed CompassTag database:

```sh
dataprocessing \
  --input compasstag.db3 \
  --output compasstag-processed.db3 \
  --processor compass-calibrated \
  --processor compass-orientation \
  --if-exists replace \
  --print-summary
```

Create an IMUTag orientation database:

```sh
dataprocessing \
  --input imutag.db3 \
  --output imutag-processed.db3 \
  --processor imu-compass-orientation \
  --print-summary
```

## Output

Each processor writes a new table, registers its columns in the stream metadata,
and records a `ProcessingRun` row with the processor id, input hash, source
tables, output tables, and configuration JSON.

Compass orientation is stored as a magnetic-frame quaternion. Heading, pitch
and roll are not stored: Euler angles depend on a rotation-order convention
that differs between libraries, so a tool that needs them computes them from
the quaternion, then applies its own declination and battery-direction
convention. Files written before version 2 of the processors also have `yaw`,
`pitch` and `roll` columns.

An IMUTag samples its magnetometer once for every ten IMU samples, so
`imu-compass-orientation` pairs each magnetometer row with the mean of the
accelerometer rows logged in the same span. Its rows keep the magnetometer
row's segment, sample index and time, so they line up with the raw IMUTag
tables, and an `AccelSamples` column records how many accelerometer rows each
mean used. A magnetometer row with no accelerometer rows beside it, or one the
solver cannot use, is skipped and counted rather than ending the run;
`--print-summary` and `--dry-run` report the counts. Tag View plots the result
when it opens the processed file.

## Troubleshooting

Use `--list-processors` first if a processor reports missing input tables or
calibration data. The CompassTag processors require a `Compass` table, and
`imu-compass-orientation` requires `ImuMag` and `ImuAccel` tables. All of them
require a `Calibration` row containing magnetometer calibration parameters,
which an IMUTag gets from [Tag Calibrator](../apps/tagcalibrate.md) or
[`tag-cal`](tag-cal.md).
