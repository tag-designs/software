# tag-reset

`tag-reset` stops active logging states and erases completed log data when
appropriate.

> Fill in: describe exactly when users should run this command and what data can
> be lost.

## Usage

```sh
tag-reset [options]
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
- Whether logs should be downloaded before reset.
- Expected starting states.

## Examples

```sh
tag-reset
```

```sh
tag-reset --base 20:7
```

## Output

Fill in: describe the status/debug output and final state messages.

## Troubleshooting

Fill in: include common messages such as "No matching device", "Attach failed",
and unexpected tag states.
