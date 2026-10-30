# base-flash

`base-flash` updates a **base's** own firmware through the bootloader built
into its microcontroller, over USB, without STM32CubeProgrammer. Once the new
firmware is written the base restarts and `base-flash` checks that it is
running the image just written.

## Usage

```sh
base-flash [options] <command>
```

| Command | Function |
| --- | --- |
| `list` | Lists the devices waiting in bootloader (DFU) mode. |
| `program <elf>` | Writes a base image, restarts the base and confirms it runs. |

## Options

| Option | Value | Function |
| --- | --- | --- |
| `-s`, `--serial` | serial | For `program`: the device to write, as `base-flash list` prints it. Needed only when more than one is in bootloader mode. |
| `-w`, `--wait` | seconds | For `program`: how long to wait for a base to appear in bootloader mode. Default 0. |
| `-d`, `--debug` | none | Enables debug logging. |
| `-h`, `--help` | none | Prints command usage and exits. |

## Preconditions

- Put the base in bootloader mode: **hold its button while plugging it into
  USB**, then release the button.
- Remove any tag from the base; its power is cut when the base restarts.
- On Linux, install the udev rules in `tag_tools/udev`, which let an
  ordinary user open a base in bootloader mode. On Windows it uses the
  WinUSB driver Windows supplies, and macOS needs nothing extra.

## Examples

```sh
base-flash list
base-flash program bittag-base-v7.elf
base-flash program --wait 60 tag-breakout-base-l432-v1.elf
```

With `--wait`, start the command first, then plug the base in with its
button held.

## Output

`program` prints the image's address range and the firmware identity it
carries, shows progress, and ends with
`Confirmed: base <serial> is running the new image`. It exits 1 if the base
does not come back reporting that identity within ten seconds, and 2 for a
mistake in the options.

## Troubleshooting

- **No device is in DFU mode.** The base started its normal firmware: unplug
  it and plug it in again with the button held.
- **The bases on USB report a different image.** The write did not take, or
  a different base was confirmed. Run `program` again.
