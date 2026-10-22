# tag-txt-import

`tag-txt-import` converts a BitTag or PresTag log saved in the old text format
(`.txt`) into the SQLite format (`.db3`) that SensorViz and the other tools
read. The text format is being retired; SQLite holds the same data together
with the tag's description, and any analysis tool can read it (see
[SQLite Log Format](../reference/sqlite-logs.md)).

The `.txt` file is only read, never changed. Each conversion is checked before
the `.db3` is kept: the tool reads the new log back, writes it out again as
text with the same code that wrote current-format `.txt` files, and compares
that with your original line by line. If they do not agree, no `.db3` is
written and the tool says which line differed.

## Usage

```sh
tag-txt-import mylog.txt -o mylog.db3
```

On success it prints one line:

```text
mylog.txt: variant C, 4550 records written to mylog.db3; round trip passed (26869 lines exact, 9531 within printed precision)
```

"Within printed precision" means the value matches once rounded the way the
original file printed it. Older versions of the download software printed
activity with fewer or more decimal places than the current one, for example
`1.33` or `0.6667` where the current software prints `1.33333`.

## Options

| Option | Value | Function |
| --- | --- | --- |
| `-o`, `--output` | SQLite file | The log to write. An existing file is replaced. Required. |
| `--report` | JSON file | Also write the conversion report (below) to this file. |
| `--root` | Directory | Record the source file's path relative to this directory, for example the top of an experiment's folder, instead of as typed. |
| `--keep-failed` | none | Keep the `.db3` when the check fails, marked as failed, instead of deleting it. For investigating a failure; do not use the result for analysis. |
| `-d`, `--debug` | none | Print the SQLite writer's debug messages. |
| `-h`, `--help` | none | Print usage and exit. |

## Converting a folder

The tool converts one file at a time. To convert every `.txt` file in a folder
and its subfolders, writing each `.db3` beside its `.txt`:

On macOS or Linux:

```sh
find my-experiment -name '*.txt' -exec sh -c 'tag-txt-import "$1" -o "${1%.txt}.db3"' _ {} \;
```

On Windows, in PowerShell:

```powershell
Get-ChildItem my-experiment -Recurse -Filter *.txt | ForEach-Object { tag-txt-import $_.FullName -o ($_.FullName -replace '\.txt$', '.db3') }
```

Files that are not logs, such as notes saved as `.txt` or the `._` files macOS
leaves on shared drives, are refused with a message and skipped.

## Which files it reads

Five generations of the text format exist. The tool recognises each from the
file itself; the report names it with a letter.

| Letter | Files | How they are checked |
| --- | --- | --- |
| **C** | BitTag logs with the current header (`Tag Type: BITTAG`), per-minute or 5-minute activity | Round trip |
| **D** | PresTag logs (`Tag Type: PRESTAG`) | Round trip |
| **B** | BitTag V5 logs (header with `Board: BitTag V5`, no `Tag Type`) | Round trip |
| **E** | BitTag logs with no header, only data lines | Round trip |
| **A** | 2019–2020 BitTag RTCv4 logs: tab-indented lines `time <date>,<epoch>,<hex>` | Translated, not checked (see below) |

### Generation A: translated, not checked

The oldest logs were written by a program that is no longer part of the
software, so there is nothing to check them against. They are translated the
same way the Bit Tag Visualizer reads them: each 5-minute line holds the
number of active seconds in each of five minutes, stored as percent activity
per minute; the `temperature T vdd V` lines become the core temperature and
voltage. The report and the log say these files were translated, not checked.

## What it changes

Some old files contain problems. The tool fixes the ones it can fix exactly,
records each fix, and refuses to guess at the rest:

| Problem | What the tool does |
| --- | --- |
| 5-minute (FIVEMIN) logs saved by software from before September 2021, which unpacked each record into nine values instead of seven | Recovers the correct seven 5-minute values; the check renders them back the old way to compare with your file |
| A record repeated exactly | Keeps one copy |
| Two different records with the same time | Keeps both and counts them |
| Records dated before 2015 or after the file was read (a tag whose clock was not set) | Drops them |
| Two downloads saved into one file, with records cut in half where they join | Does not convert the file: which half-record belongs where cannot be told |
| Windows line endings, a missing final newline, comment lines such as `# End of File` | Handled |

Every dropped or removed line is listed, word for word, in the report.

**PresTag voltages.** PresTag firmware recorded a second supply-voltage
reading where a core temperature would go, so the `TC:` value in a PresTag
`.txt` file is a voltage, not a temperature. The tool warns about this, stores
the average of the `V:` and `TC:` values as the log's single voltage, and
writes no core temperature. This matches a PresTag log downloaded directly to
SQLite, which also records one voltage.

## What it writes

The `.db3` has the same tables as a log downloaded directly from the tag:

- BitTag: `Activity` (percent active), `Voltage`, `CoreTemperature`;
- PresTag: `Voltage`, `Pressure` (mbar), `Temperature` (°C).

The `info` table also records where the log came from:

| Field | Contents |
| --- | --- |
| `source` | `txt-import` |
| `txt_source_path`, `txt_source_sha256`, `txt_source_size` | The original file: its path, a SHA-256 fingerprint and its size, so the conversion can be traced to the exact file |
| `txt_variant` | The format letter above |
| `txt_header` | The original header lines, unchanged |
| `txt_roundtrip` | `passed`, or for generation A, `translated: …` |
| `txt_conversion_report` | The conversion report |

The **conversion report** lists the format, the number of records read and
written, each kind of fix with its count, and every removed line. It is
stored in the log and, with `--report`, written as a JSON file.

## Exit status

| Status | Meaning |
| --- | --- |
| 0 | Converted and checked (generation A: translated) |
| 1 | Not converted: the file is damaged in a way the tool will not guess at, or the check failed. The message says why |
| 2 | Not a log |

## Troubleshooting

| Message | Meaning and what to do |
| --- | --- |
| `round trip failed: line N: …` | The converted log does not reproduce line N of your file, so nothing is written. Please report it with the file; `--keep-failed` keeps the attempted log for investigation. |
| `a V: line with no activity before it (exports run together?)` or `activity that no V: line closes` | Records were cut in half, usually where two downloads were saved into one file (often at a `# End of File` line). The tool does not guess which half belongs where, so the file cannot be converted as it is. If another copy of the same download exists, try that; otherwise report it. |
| `not a log` | The file is not a tag log: notes, an empty file, or a macOS `._` file. |
| `warning: … PresTag TC: holds a second supply voltage` | Expected for every PresTag file; see [What it changes](#what-it-changes). |
