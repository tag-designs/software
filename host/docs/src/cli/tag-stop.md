# tag-stop

`tag-stop` attaches to a tag, sends the stop command, waits for the tag to
reach `FINISHED` or `ABORTED`, and prints its status.

A tag accepts a stop and acts on it a moment later. If the tag is still
collecting a few seconds after accepting the stop, `tag-stop` sends it again,
because a tag stopped straight after it was started can accept the first stop
and never act on it. A tag that is already `FINISHED` or `ABORTED` is reported
as it is, with no stop sent. A tag in any other state that is not
collecting, that is not `CONFIGURED`, `RUNNING` or `HIBERNATING`, is not sent a
stop and the command fails.

## Usage

```sh
tag-stop [options]
```

## Options

| Option | Value | Function |
| --- | --- | --- |
| `-d`, `--debug` | none | Enables debug logging. |
| `-b`, `--base` | `BUS:DEVICE` | Selects a specific USB device by bus and device address. |
| `--stop-timeout` | seconds | How long to wait for the tag to reach a stopped state after the stop is accepted. Default 30. |
| `--settle-timeout` | seconds | How long to wait after connecting for the tag to report its state. Connecting restarts the tag, which reports no definite state while it boots. Default 10. |
| `-h`, `--help` | none | Prints command usage and exits. |

## Preconditions

- A compatible tag/base interface is connected over USB.
- Use `--base BUS:DEVICE` when more than one compatible interface is connected.
- Download any needed logs before stopping if the active run should continue.

## Examples

```sh
tag-stop
```

```sh
tag-stop --base 20:7
```

## Output

The command prints the protobuf `Status` message read once the tag has stopped.
A tag stopped by the command is `FINISHED`. The status is printed when the tag
fails to stop, or is not collecting, too.

## Troubleshooting

- `No matching device`: no connected tag/base matched the optional `--base`
  selector.
- `Attach failed`: the USB interface was found but could not be opened.
- `Stop failed`: the tag refused the stop command, or did not answer it.
- `Tag is IDLE, not acquiring; no stop sent`: the tag had no run to stop. The
  same message names any other state that is not collecting.
- `Tag did not reach a stopped state in time`: the tag was still collecting at
  the end of `--stop-timeout`, after the stop was sent more than once.
- `Could not read tag status`: the command was sent, but the follow-up status
  request failed.
