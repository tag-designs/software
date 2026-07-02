# tag-dwnld

`tag-dwnld` downloads stored log records from a tag and writes them to a file.
Use it for scripted downloads, recovery work, or cases where the GUI monitor is
not convenient.

## Usage

```sh
tag-dwnld [options]
```

## Options

| Option | Value | Function |
| --- | --- | --- |
| `-o`, `--output` | path | Output file path. If omitted, SensorTag tools write `tag-download.txt` or `tag-download.db3` depending on the selected/default format. |
| `-f`, `--format` | `default`, `text`, `sqlite` | Selects the output storage format. `default` chooses the default format for the loaded tag type. Accepted SQLite aliases in the parser include `sqlite`, `sql`, `db`, and `db3`; accepted text aliases include `txt` and `text`. |
| `-s`, `--stop` | none | Stops a running tag before downloading. Without this option, download proceeds only when the tag is already finished, aborted, or in exception state. |
| `--rescue-exception` | none | For old firmware that refuses downloads from `EXCEPTION`, changes the monitor-visible backup state to `ABORTED` before downloading. This does not erase log flash or reset log counters. |
| `-d`, `--debug` | none | Enables debug logging. |
| `-b`, `--base` | `BUS:DEVICE` | Selects a specific USB device by bus and device address. |
| `-h`, `--help` | none | Prints command usage and exits. |

## Preconditions

- A compatible tag/base interface is connected over USB.
- Normal downloads require `FINISHED`, `ABORTED`, or `EXCEPTION`.
- `--stop` can stop a running tag before download. Without it, a running tag is
  rejected so active log storage is not read while it is still being written.
- `--rescue-exception` is for old BitTag firmware recovered from the field. It
  changes only the monitor-visible backup state from `EXCEPTION` to `ABORTED`,
  then reads legacy BitTag log headers directly from internal flash if the
  firmware cannot serve data-log RPCs. It does not erase flash or reset counters.

## Examples

```sh
tag-dwnld --format sqlite --output my-log.db3
```

```sh
tag-dwnld --stop --format default
```

```sh
tag-dwnld --rescue-exception --format sqlite --output field-rescue.db3
```

```sh
tag-dwnld --rescue-exception --format text --output field-rescue.txt
```

## Output

The command prints the selected output path, a progress bar, the final record
count, and timing information. SQLite output stores metadata in `info`, state
history in `states`, stream metadata in `streams`, and tag-specific data tables
such as `Activity`, `Voltage`, and `CoreTemperature`. Text output writes a
comment header followed by line-oriented log records.

For BitTag rescue downloads, the raw 64-bit activity field is not interpreted by
the rescue reader. It is wrapped in a normal `BitTagLog` payload and decoded by
the selected writer using the recovered `Config.bittag_log` value.

## Troubleshooting

- `No matching device`: no connected tag/base matched the optional `--base`
  selector.
- `Attach failed`: the USB interface was found but could not be opened.
- `Can't dump logs from current state`: the tag is not in a terminal state and
  `--stop` or `--rescue-exception` did not make it downloadable.
- `Could not scan BitTag log headers`: the rescue path could not read legacy
  BitTag internal flash headers.
- `Could not write log data`: the writer could not decode or store a returned
  payload. Check that the selected format supports the tag type.
