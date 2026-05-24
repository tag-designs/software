# Command-Line Tools

The host command-line tools live in `host/commandline` and are built with the
host tools. They are intended for direct USB tag operations and scripting.

Use these pages as working templates. Fill in the purpose, preconditions,
examples, expected output, and troubleshooting details as each tool is reviewed
against the current workflow.

## Common Options

Most tag-attached tools share these options:

| Option | Value | Function |
| --- | --- | --- |
| `-d`, `--debug` | none | Enables debug logging. Without this option, the tools use error-level logging. |
| `-b`, `--base` | `BUS:DEVICE` | Selects a specific USB device by bus and device address. Use this when more than one compatible base/tag interface is connected. |
| `-h`, `--help` | none | Prints command usage and exits. |

## Tool Pages

| Tool | Template page | Starting point |
| --- | --- | --- |
| `tag-dwnld` | [tag-dwnld](tag-dwnld.md) | Download finished or aborted tag logs to text or SQLite files. |
| `tag-info` | [tag-info](tag-info.md) | Print tag information, firmware metadata, status/configuration details. |
| `tag-reset` | [tag-reset](tag-reset.md) | Stop a running/hibernating tag and erase completed log data. |
| `tag-start` | [tag-start](tag-start.md) | Start logging when a tag is idle. |
| `tag-cal` | [tag-cal](tag-cal.md) | Start calibration mode and stream raw calibration samples. |
| `tag-test` | [tag-test](tag-test.md) | Run RTC checks and tag self-tests. |
| `tag-test-example` | [tag-test-example](tag-test-example.md) | Minimal RTC-set example for developers. |
| `tag-monitor-test` | [tag-monitor-test](tag-monitor-test.md) | Developer monitor-interface test tool. |

## Fill-In Checklist

Use the same structure on each tool page:

1. Replace the placeholder description with the user-facing job the tool
   performs.
2. Add preconditions such as required tag state, firmware support, calibration
   setup, or expected hardware connection.
3. Add one or more known-good commands.
4. Paste representative output or describe the output fields.
5. Add troubleshooting notes for common failure messages.
