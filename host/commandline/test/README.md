---
type: readme
status: current
summary: Offline check of tag-txt-import - anonymised fixtures of each legacy .txt variant and repair, and the script that converts them and asserts each conversion report.
---

# tag-txt-import Check

[`txt_import_check.py`](txt_import_check.py) runs `tag-txt-import` on every
fixture in [`txt-import/`](txt-import/) and asserts what its conversion report
must say: the variant, the records written, each repair, and that the round
trip back to text passed. It also checks the written log's row counts and
provenance; that exports run together fail; and that variant A and a non-log
are refused. Run it by hand after
building the tool:

```sh
python3 host/commandline/test/txt_import_check.py <build-dir>/Release/bin/tag-txt-import
```

It needs only Python 3's standard library. There is no test framework; it
stops at the first failure.

## The fixtures

Each is a few records trimmed from a file in the legacy collection, with the
UUID replaced by zeros and the firmware repository URL by a placeholder:

| Fixture | Variant | What it exercises |
| --- | --- | --- |
| `c1-fivemin.txt` | C | Current header, FIVEMIN, an older writer's fixed decimals |
| `c1-fivemin-crlf.txt` | C | The same with CRLF line endings |
| `c1-duplicate-record.txt` | C | A record repeated exactly: removed |
| `c1-implausible-time.txt` | C | A record dated 2005: dropped |
| `c2-min.txt` | C | MIN, ten 6-bit buckets |
| `c3-packed-word.txt` | C | The packed word on `V:` lines, checked against the buckets |
| `b-fivemin-9x7.txt` | B | The V5 header and the pre-2021 nine-bucket FIVEMIN decode, repaired |
| `d-prestag.txt` | D | PresTag blocks; `V:` and `TC:` (both voltages) stored as their mean |
| `e-no-header.txt` | E | No header; format from the data |
| `e-concatenated.txt` | E | Two exports run together, with orphan and unclosed records: must fail |
| `header-only.txt` | C | A header with no data |

When a fixture's expected counts change, update `EXPECT` in the script and say
why in the commit.
