# tag-info

`tag-info` prints tag/base identity, firmware metadata, current configuration,
and related protobuf fields.

> Fill in: describe the main support/debug workflow this tool serves and which
> fields are most useful to copy into issue reports.

## Usage

```sh
tag-info [options]
```

## Options

| Option | Value | Function |
| --- | --- | --- |
| `-d`, `--debug` | none | Enables debug logging. |
| `-b`, `--base` | `BUS:DEVICE` | Selects a specific USB device by bus and device address. |
| `-h`, `--help` | none | Prints command usage and exits. |

## Preconditions

Fill in:

- Required tag/base connection.
- Whether the tag can be running, idle, finished, or hibernating.

## Examples

```sh
tag-info
```

```sh
tag-info --base 20:7 --debug
```

## Output

Fill in: describe the printed SHA, tag information fields, firmware/build
metadata, and configuration dump.

## Troubleshooting

Fill in: include common messages such as "No matching device", "Attach failed",
and "Info failed".
