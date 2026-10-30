# tag-flash

`tag-flash` programs, erases and reads a tag's internal flash through its
base, without STM32CubeProgrammer. It programs the way
[Tag Programmer](../apps/tagprogram.md) does, from the command line, for
scripts and for one-off programming at the bench.

**Every command except `list` restarts the tag**: connecting to a tag's
debug port is only possible by holding it in reset. A tag that was logging
stops logging.

## Usage

```sh
tag-flash [options] <command>
```

| Command | Function |
| --- | --- |
| `list` | Lists the bases on USB, with each one's serial and the firmware it reports. |
| `info` | Connects and reports the tag's microcontroller, flash size and supply voltage. |
| `program <elf>` | Programs an image, verifies it and starts it. |
| `erase` | Erases the whole of internal flash and programs nothing. |
| `read <address> <length> -o <file>` | Reads memory into a file. Address and length are decimal or `0x` hex. |

## Options

| Option | Value | Function |
| --- | --- | --- |
| `-b`, `--base` | serial | Selects a base by its USB serial, as `tag-flash list` prints it. Needed only when more than one base is connected. Unlike the other tools' `BUS:DEVICE`, a serial does not change when the base is unplugged. |
| `--erase` | `image`, `plan` or `all` | For `program`: what to erase first. `image` (the default) erases only the pages the image occupies. `plan` erases what Tag Programmer would for this image, which keeps a calibrated STM32L432 tag's calibration. `all` erases everything. |
| `-d`, `--debug` | none | Enables debug logging. |
| `-h`, `--help` | none | Prints command usage and exits. |

## Preconditions

- The tag is on a base connected by USB.
- Before programming a tag that holds a log, download the log and erase it
  with [tag-reset](tag-reset.md). The firmware that wrote a log is the one
  that knows how to erase it.

## Examples

```sh
tag-flash list
tag-flash info
tag-flash program BitTag.elf
tag-flash --base 004D0059543050162030324E program CompassTag.elf --erase plan
```

After programming, run [tag-test](tag-test.md) to check the tag and the new
image agree.

## Output

`program` prints the image and the microcontroller, shows progress, and ends
with `Programmed, verified and started`. The exit status is 0 on success, 1
on failure and 2 for a mistake in the options.

## Troubleshooting

- **No base is on USB.** Check the cable, and on Linux that the udev rules in
  `tag_tools/udev` are installed.
- **Several bases are connected.** Pick one with `--base`, using a serial
  from `tag-flash list`.
- **A tool run straight after `tag-flash` cannot connect.** `tag-flash` waits
  a second before it exits so the base is ready; if another tool still fails,
  run it again.
