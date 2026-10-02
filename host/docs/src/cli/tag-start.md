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

After the start is accepted, `tag-start` reads the tag's state until it leaves
IDLE, and prints it:

- `State: RUNNING` or `State: CONFIGURED` -- logging has started, or will start
  at the configured time. Exit status 0.
- `State: not confirmed (last read: ...)` -- the start was accepted, and the
  tag then left the debug link before its new state could be read. This is
  normal for tags that go to sleep as soon as they start, such as UIUCTag: a
  tag must give up its debug interface to sleep. Exit status 0. To confirm,
  run `tag-start` again; it reports `Start skipped: tag is already RUNNING`.
- Any other state, such as ABORTED, or a tag still IDLE after
  `--start-timeout` seconds -- the start failed. Exit status 1.

## Troubleshooting

Fill in: include common messages such as "No matching device", "Attach failed",
and cases where the tag is not idle.
