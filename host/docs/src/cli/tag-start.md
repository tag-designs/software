# tag-start

`tag-start` starts logging with the tag's current configuration when the tag is
idle.

> Fill in: describe how the tag should be configured before using this command
> and how users should confirm that logging has started.

## Usage

```sh
tag-start [options]
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
- Required tag state.
- Required configuration workflow before starting.

## Examples

```sh
tag-start
```

```sh
tag-start --base 20:7
```

## Output

Fill in: describe the printed final state and what state indicates success.

## Troubleshooting

Fill in: include common messages such as "No matching device", "Attach failed",
and cases where the tag is not idle.
