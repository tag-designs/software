# dataprocessing

`dataprocessing` copies a downloaded SQLite log and adds computed sensor
streams to the copy. The original database is left unchanged so the downloaded
log remains the record of what came from the tag.

The first processors support CompassTag logs:

| Processor | Added table | Function |
| --- | --- | --- |
| `compass-calibrated` | `CompassCalibrated` | Copies accelerometer samples and writes calibrated magnetometer x/y/z values. |
| `compass-orientation` | `CompassOrientation` | Writes the orientation quaternion (`qw`, `qx`, `qy`, `qz`), dip, field strength, and acceleration magnitude columns. |

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

## Troubleshooting

Use `--list-processors` first if a processor reports missing input tables or
calibration data. Compass processors require a `Compass` table and a
`Calibration` row containing magnetometer calibration parameters.
