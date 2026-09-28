# tag-info

`tag-info` prints tag/base identity, firmware metadata, current configuration,
and related protobuf fields.

> Fill in: describe the main support/debug workflow this tool serves and which
> fields are most useful to copy into issue reports.

## Usage

```sh
tag-info [options]
```

## Options

| Option | Value | Function |
| --- | --- | --- |
| `-d`, `--debug` | none | Enables debug logging. |
| `-b`, `--base` | `BUS:DEVICE` | Selects a specific USB device by bus and device address. |
| `-j`, `--json` | none | Prints everything as a single JSON object on one line, for recording rather than reading. |
| `-h`, `--help` | none | Prints command usage and exits. |

## Preconditions

Fill in:

- Required tag/base connection.
- Whether the tag can be running, idle, finished, or hibernating.

## Examples

```sh
tag-info
```

```sh
tag-info --base 20:7 --debug
```

Record a tag's identity when it is programmed:

```sh
tag-info --json >> ~/tags/programmed.jsonl
```

## Recording what is on a tag

`--json` exists for the board database. A tag's UUID, the commit it was built
from and when that build was compiled are recorded when the tag is programmed,
and parsing those back out of labelled prose is the kind of step that silently
rots. The protobuf messages are serialized by the library with the same options
`tagcore` uses when it stores them, so a field added to the protocol appears in
the JSON without this tool being changed.

One field is not there and cannot be: **the SHA-256 of the image**. An image
cannot contain its own hash, so no tag can report which *build* of a commit it
is running -- only which commit. `build_time` distinguishes builds to the
second, but it records when `monitor.c` was compiled rather than when the image
was linked, so an incremental rebuild can change the image without changing it.
The image hash is available only when the tag is programmed:
`embedded/tools/flash_release.py` prints it, and records it with `--json`.

Those two records are not automatically joinable, and deliberately so. A batch
flashed from one release shares a commit and an image hash across every record,
distinguished only by the board label the operator supplies at flash time, while
the tag knows its UUID and not its label. Pairing them is a per-tag step at the
moment of entry -- flash one, read one, enter one row.

## Output

Fill in: describe the printed SHA, tag information fields, firmware/build
metadata, and configuration dump.

## Troubleshooting

Fill in: include common messages such as "No matching device", "Attach failed",
and "Info failed".
