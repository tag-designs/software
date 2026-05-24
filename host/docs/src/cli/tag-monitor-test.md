# tag-monitor-test

`tag-monitor-test` is a developer tool for testing the debug monitor interface
through the link adapter.

> Fill in: describe the intended hardware setup, firmware requirements, and what
> a successful monitor attach proves.

## Usage

```sh
tag-monitor-test [options]
```

## Options

| Option | Value | Function |
| --- | --- | --- |
| `-d`, `--debug` | none | Enables debug logging. |
| `-b`, `--base` | `BUS:DEVICE` | Selects a specific USB device by bus and device address. |
| `-h`, `--help` | none | Prints command usage and exits. |

## Preconditions

Fill in:

- Required ST-Link/base connection.
- Required monitor-capable firmware.
- Any reset/debug-state assumptions.

## Examples

```sh
tag-monitor-test --debug
```

```sh
tag-monitor-test --base 20:7 --debug
```

## Output

Fill in: describe the expected debug messages, monitor version/hash checks, and
what counts as success.

## Troubleshooting

Fill in: include common messages such as "No matching device", "Attach failed",
"monitor call timed out", and monitor version/hash read failures.
