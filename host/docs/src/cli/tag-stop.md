# tag-stop

`tag-stop` attaches to a tag, sends the stop command, and prints the status read
back after the command completes.

## Usage

```sh
tag-stop [options]
```

## Options

| Option | Value | Function |
| --- | --- | --- |
| `-d`, `--debug` | none | Enables debug logging. |
| `-b`, `--base` | `BUS:DEVICE` | Selects a specific USB device by bus and device address. |
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

The command prints the protobuf `Status` message returned after the stop command.
On a successfully stopped active tag, the state is typically `ABORTED`.

## Troubleshooting

- `No matching device`: no connected tag/base matched the optional `--base`
  selector.
- `Attach failed`: the USB interface was found but could not be opened.
- `Stop failed`: the monitor did not acknowledge the stop command.
- `Could not read tag status`: the command was sent, but the follow-up status
  request failed.
