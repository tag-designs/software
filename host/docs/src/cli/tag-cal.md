# tag-cal

`tag-cal` starts calibration mode and streams raw accelerometer/magnetometer
calibration samples until interrupted.

> Fill in: describe the physical calibration procedure, how to move the tag,
> and where the captured sample output should be saved or pasted.

## Usage

```sh
tag-cal [options]
```

The executable is named `tag-cal`; the internal help title may refer to
`tag-calibrate`.

## Options

| Option | Value | Function |
| --- | --- | --- |
| `-d`, `--debug` | none | Enables debug logging. |
| `-b`, `--base` | `BUS:DEVICE` | Selects a specific USB device by bus and device address. |
| `-h`, `--help` | none | Prints command usage and exits. |

## Preconditions

Fill in:

- Required tag/base connection.
- Required tag state. The current code starts calibration only when the tag is
  idle.
- Sensor/firmware requirements for calibration data.

## Examples

```sh
tag-cal
```

```sh
tag-cal --base 20:7 > calibration-raw.csv
```

## Output

Fill in: describe the `Raw:` sample lines and the accelerometer/magnetometer
column order.

## Troubleshooting

Fill in: include common messages such as "No matching device", "Attach failed",
and "Tag not idle".
