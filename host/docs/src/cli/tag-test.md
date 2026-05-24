# tag-test

`tag-test` is a developer/support tool that reads tag information, checks and
sets the RTC, prints state history, and runs tag self-tests when the tag is idle.

> Fill in: describe who should run this tool, how long it normally takes, and
> what pass/fail result users should report.

## Usage

```sh
tag-test [options]
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
- Required tag state. The self-test portion runs only when the tag is idle.
- Whether data should be downloaded before running tests.

## Examples

```sh
tag-test
```

```sh
tag-test --debug
```

## Output

Fill in: describe tag identity fields, voltage, RTC drift, state log entries,
and the final test result.

## Troubleshooting

Fill in: include common messages such as "No matching device", "Attach failed",
and "State not idle".
