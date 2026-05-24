# tag-dwnld

`tag-dwnld` downloads stored log records from a tag and writes them to a file.

> Fill in: describe when this tool should be used instead of the GUI downloader,
> which tag states are expected, and which output format is preferred for each
> tag family.

## Usage

```sh
tag-dwnld [options]
```

## Options

| Option | Value | Function |
| --- | --- | --- |
| `-o`, `--output` | path | Output file path. If omitted, SensorTag tools write `tag-download.txt` or `tag-download.db3` depending on the selected/default format. |
| `-f`, `--format` | `default`, `text`, `sqlite` | Selects the output storage format. `default` chooses the default format for the loaded tag type. Accepted SQLite aliases in the parser include `sqlite`, `sql`, `db`, and `db3`; accepted text aliases include `txt` and `text`. |
| `-s`, `--stop` | none | Stops a running tag before downloading. Without this option, download proceeds only when the tag is already finished or aborted. |
| `-d`, `--debug` | none | Enables debug logging. |
| `-b`, `--base` | `BUS:DEVICE` | Selects a specific USB device by bus and device address. |
| `-h`, `--help` | none | Prints command usage and exits. |

## Preconditions

Fill in:

- Required tag/base connection.
- Required tag state before download.
- What happens when the tag is running and `--stop` is not supplied.

## Examples

```sh
tag-dwnld --format sqlite --output my-log.db3
```

```sh
tag-dwnld --stop --format default
```

## Output

Fill in: describe the progress bar, final record count, output file contents,
and any format-specific notes.

## Troubleshooting

Fill in: include common messages such as "No matching device", "Attach failed",
"Can't dump logs from current state", unsupported tag type/format combinations,
and output file write failures.
