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
| `--set-rtc` | none | Sets the tag's clock from the computer once the tag is `IDLE`. |
| `--stop-timeout` | seconds | How long to wait for a running tag to stop before erasing. Default 30. |
| `--reset-timeout` | seconds | How long to wait for the erase to finish. Default 180. |
| `--settle-timeout` | seconds | How long to wait after connecting for the tag to report its state. Default 10. |
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

`tag-reset` stops a tag that is `RUNNING`, `HIBERNATING` or `CONFIGURED`,
erases a tag that is `FINISHED` or `ABORTED`, and ends with
`Final state: IDLE` and exit status 0. A tag in any other state, such as
`TEST` or `EXCEPTION`, is not reset: the final state is printed with an
explanation, and the exit status is 1.

## Troubleshooting

Fill in: include common messages such as "No matching device", "Attach failed",
and unexpected tag states.
