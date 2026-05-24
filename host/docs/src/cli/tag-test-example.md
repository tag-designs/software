# tag-test-example

`tag-test-example` is a minimal developer example that sets the tag RTC, waits,
then prints the measured clock error.

> Fill in: describe whether this is intended for end users, developers, or
> smoke testing only.

## Usage

```sh
tag-test-example [options]
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
- Expected tag state and firmware support for RTC setting/status reads.

## Examples

```sh
tag-test-example
```

## Output

Fill in: describe the `Clock Error:` value and expected range.

## Troubleshooting

Fill in: include common messages such as "No matching device" and attach/status
read failures.
