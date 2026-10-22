---
type: proposal
status: proposed
summary: Retire the legacy .txt sensor-log format and btviz - convert the existing BitTag and PresTag .txt collection to SQLite with a command-line importer that round-trip-verifies every conversion, use the conversion and a dual-format field download to prove the SQLite writer on real experiments, then delete the text writer, btviz and their docs; actograms and filtering move to general-purpose tools reading SQLite.
---

# Retiring the Text Log Format and btviz

**Status:** phases 0 and 1 built (2026-10-22), with most of phase 2's
checking; the rest proposed.

- **Built:** the deprecation warnings (phase 0); `tag-txt-import` for
  variants B, C, D and E with the decoded-sample writer entry
  (`SqliteTagLogWriter::writeSamples()`), provenance, the repairs and the
  round-trip check on every conversion; anonymised fixtures with a check
  script ([`host/commandline/test/`](../../../../commandline/test/README.md)).
- **PresTag `TC:` is a voltage.** PresTag firmware put a second supply
  voltage in the core-temperature slot. The importer stores the mean of
  `V:` and `TC:` as one `Voltage` and no `CoreTemperature`, and warns; a
  direct SQLite download keeps `V:` alone. Variant D's mapping above is
  superseded.
- **Exports run together** (a `V:` line with no activity before it, or
  activity no `V:` line closes) fail the conversion rather than being
  guessed at.
- **On the whole collection:** 398 files of variants B–E convert and pass
  the round trip (232 byte for byte, the rest within the source's printed
  precision from older writers), including the 69 repaired FIVEMIN files
  and all 285,821 packed words. 2 fail as exports run together.
  195 variant A files and 24 non-logs
  are refused. After the 9×7 repair, only 4 exact duplicate records remain
  and none conflict: most "duplicates" in the survey were that bug's
  overlapping bucket times.
- **Not built yet:** variant A; `--batch` with the collection report and
  manifest; the pairing of repaired originals with their re-exports; the
  sensorviz fixtures; phases 3–5.

The `.txt` sensor-log format and btviz, the tool that reads it, are to be
retired. SQLite (`.db3`) becomes the only log format and sensorviz the only
viewer. The plan has three parts:

1. **Migrate the data.** A command-line importer, `tag-txt-import`, converts
   the existing BitTag and PresTag `.txt` logs to SQLite logs in tagcore's
   own schema. It verifies every conversion by rendering the result back to
   text with the original writer's code and comparing it with the source
   file. Each converted log records where it came from and that it passed.
2. **Prove the SQLite writer on real data.** The SQLite path has not yet been
   used on a real experiment. The conversion exercises its schema at scale.
   The two `.txt`-era tags now in the field are downloaded in both formats
   (plus raw Acks) and cross-checked before anything is deleted.
3. **Delete.** The text writer, btviz, their documentation, screenshots,
   packaging and btviz-only third-party code are removed. sensorviz does not
   take over btviz's analysis features. Actograms, filtering and interval
   statistics belong to general-purpose analysis tools, which read the SQLite
   logs directly.

## What exists

### The text writer and its formats

`TextTagLogWriter` (`txtlogs.cc`) writes a `#`-commented header (tag info,
calibration, configuration, state log), then data lines of the form
`epoch,KEY:value`, with integer Unix seconds. It covers every tag type, but
only BitTag and PresTag `.txt` logs exist in practice. Selected by
`tag-dwnld -f text` and in qtmonitor's save dialog; the default is already
SQLite for every type that supports it.

### The collection

A legacy collection, a private git repository of about 2.1 GB, holds 619
`.txt` files (730 MB) from experiments between 2019 and 2023. Only BitTag and
PresTag logs are present. The survey (2026-10-22) found five variants:

| Variant | Files | Header | Data lines |
| --- | --- | --- | --- |
| **A**, early BitTag (RTCv4 firmware) | 192 | Free-form `#` lines (`pages`, `device`, `hash`, `info`, `# Time … epoch: N error E`); 55 files have only the time line | Tab-indented `time <local date>,<epoch>,<payload>`: a 32-bit hex activity bitmap every 5 min, or `temperature T vdd V` |
| **B**, BitTag V5 header | 100 | `Tag Read time`, `Board: BitTag V5`, `Git Hash`, `UUID`, accelerometer settings, `Format: BITSPERFIVEMIN` or `BITSPERMIN` | `epoch,FIVEMIN:pct` or `MIN:pct`; `epoch,V:v,TC:t` |
| **C**, current `writeTextHeader` | 285 | The current header (tag type, firmware, board, UUID, `BitTag Log Format`, hibernate windows, state log) | C1 `FIVEMIN` (197 files), C2 `MIN` (94); C3/C4 (32 and 19 files) add a fourth field to the `V:` line: the record's raw packed activity word (see below) |
| **D**, PresTag | 12 | Variant C header with `Tag Type: PRESTAG`, `Sample Period` | `V:,TC:`, `P:` (hPa, unstated), `T:` (°C, unstated) |
| **E**, no header | 3 | None | Variant C1 data lines |

Problems a converter must handle, all found in the collection:

- **Empty files:** 3 empty files, and 53 files with a header but no data.
  Some of the latter have a data-bearing copy under another name.
- **Duplicate files:** 50 groups of byte-identical files, 79 redundant copies
  in all.
- **Duplicate records:** BitTag V5/V6 exports contain repeated and
  overlapping records (one file: 130,645 records, 101,610 distinct times).
  Later re-exports (`*-reformat`, `*-repair`) of the same tags remove the
  duplicates and print finer values. The collection therefore holds two
  versions of those tags.
- **Corrupt times and values:** epochs of 0, of 1979 and of 2004, a
  temperature of 1839.6 and a voltage of 140.81, and time running backwards
  in some files.
- **Text encoding:** CRLF line endings (7 files) and missing final newlines
  (2). There is no non-UTF-8 text in any real log.
- **Not logs:** experiment notes and macOS AppleDouble files with a `.txt`
  extension.

**Metadata outside the logs.** No log has a sidecar file. Tag identity is in
the header for variants B–D (UUID), and only a non-UUID device string for A.
The collection's inventory (a SQLite database plus CSVs) maps tag labels to
UUIDs, experiments, battery and weight. Bird and deployment identities appear
only in file and directory names.

### The SQLite side

The SQLite writer for BitTag (MIN, FOURMIN, FIVEMIN, PERSEC) and PresTag
writes `Activity` (%), `Voltage` (V), `CoreTemperature` (°C), `Pressure`
(mbar) and `Temperature` (°C), with `streams` metadata, `info`, `states`,
`Calibration` and `schema_info`. It is fed from raw `Ack` protobufs.

**A `.txt` file holds decoded values, not Acks.** So no existing path, neither
`tag-log-import` (Acks) nor `tag-rebuild` (captures), can take one. There is
no converter.

**BitPresTag inconsistency.** The text writer packs BitPresTag activity as
5×6-bit 60 s buckets; the SQLite writer as 4×4-bit 15 s buckets
(`sqlitelog/README.md`). No BitPresTag data exists in either format, so this
is fixed in code (phase 5), not migrated.

### btviz's features

Beyond viewing, btviz offers:

- an actogram (single or double plot) with sun-elevation (SOLPOS) and
  lab-light overlays;
- a 101-tap Hanning FIR low-pass filter;
- CSV export of the visible range;
- the mean activity over the visible range, and averages over intervals read
  from a CSV;
- PNG and PDF save.

**None of these move into sensorviz.** sensorviz stays a viewer. Once the
data is in SQLite, actograms, filtering and interval statistics are better
served by general-purpose analysis tools that read the logs directly. Which
tools the researchers use is outside this project's scope. SQLite is what
makes that choice free, since any language or analysis environment can read
it. The
schema is documented in the user manual's
[SQLite logs reference](../../../../docs/src/reference/sqlite-logs.md), and
any language with SQLite can read it.

## Design

### A decoding bug in older FIVEMIN exports

**The bug.** Before `NanoTag` commit `4670705` ("fix fiveminute bug",
2021-09-25), the host writer unpacked each FIVEMIN record as **nine 7-bit
buckets** instead of seven 9-bit ones. So FIVEMIN `.txt` files written by an
unfixed host hold wrongly decoded activity, with the wrong values at the
wrong times.

**Which files.** It is detectable from the file alone: such files have 9
activity lines per record instead of 7.

- 69 of the collection's 230 FIVEMIN files have that signature.
- The `-reformat` and `-repair` re-exports, which explain why the collection
  holds two versions of those tags, were made with the fixed writer.
- **The re-exports are authoritative.** They are the corrected files already
  given to the researchers when the bug was found. The importer's repair is
  an independent check on them, and covers any buggy original that has no
  re-export.
- Where the version is not clear: the header's `Git Hash` is the tag
  *firmware's* commit, reported by the tag, not the host writer's. So the
  file cannot say which writer made it, but the 9-per-record signature can.

**It is exactly repairable.** Nine 7-bit buckets and seven 9-bit buckets
cover the same 63 bits of the stored word. So the importer:

1. rebuilds the word from the buggy percentages, `count = round(pct × 3)`;
2. decodes it correctly;
3. re-times the buckets at `t − 300 × (6 − i)`.

**Validation (2026-10-22).** On 52 files from four experiments that have
re-exports, all 2,461,788 buckets the repair produced at times shared with
the re-export agree with it, to 0.0033 % at most. That residue is the
re-exports' printed precision.

**In the importer:**

- Repair is automatic and recorded as a repair kind (`fivemin-9x7`) in the
  conversion report and `info`.
- The round trip renders the repaired log back through the *buggy* layout,
  to compare with the original file exactly.
- The batch report pairs each repaired original with its re-export where one
  exists, and records whether they agree.

### The importer: `tag-txt-import`

`tag-txt-import` is a C++ command-line tool in `host/commandline`, linking
tagcore. It sits beside the SQLite writer that owns the schema, so the
schema stays defined in one place.

**A new writer entry point for decoded samples.**

- `SqliteTagLogWriter` gains a path that writes already-decoded samples into
  the same tables and `streams` metadata as the Ack path: activity, voltage,
  core temperature, pressure and temperature.
- The Ack path is unchanged.
- Re-packing percentages into Ack bitfields was considered and rejected:
  - variant A's bitmap geometry has no Ack equivalent;
  - variant B values printed to two decimals only;
  - it would make the importer depend on details it does not need.

**Parsing.**

- One parser per variant. A file is classified from its header, or from its
  data lines for variant E.
- Lines are accepted with explicit checks: btviz's parser indexes without
  bounds checks and turns parse failures into 0.
- CRLF and a missing final newline are handled.

**Variant mappings:**

- **Variant A:** unpacks the 32-bit bitmap as btviz does: five 6-bit minute
  buckets per 5-minute record, as per-minute activity. `temperature/vdd`
  becomes `CoreTemperature` and `Voltage`. The epoch is authoritative; the
  local wall-clock text is ignored.
- **Variants B, C, E:** `MIN`, `FOURMIN` and `FIVEMIN` percentages go straight
  into `Activity`, at the times the text writer used.
- **Variant D:** `P:` becomes `Pressure` (hPa = mbar), `T:` becomes
  `Temperature`, and `V:,TC:` becomes `Voltage` and `CoreTemperature`.
- **C3/C4's fourth field** is the record's raw packed activity word: the
  64-bit value the tag stored, from which the writer unpacked the buckets.
  - *Checked on the whole collection (2026-10-22):* in all 51 files, all
    285,821 records' fields equal their own activity buckets re-packed
    (`count_i << i·bits`, 9 bits for FIVEMIN, 6 for MIN), including all
    232,480 that are nonzero.
  - *So it carries no new data.* The importer re-packs each record and
    requires equality, recording the result in the conversion report, and
    stores no extra stream.
  - *Its source:* a host writer of the `NanoTag` era that is not in that
    repository's history; no committed writer there or here prints it.

**Repairs, never silent.** Each run writes a conversion report and records
the same facts in the log's `info`:

- **Exact duplicate records:** removed.
- **Conflicting duplicates** (same time, different value): kept, flagged and
  counted.
- **Times outside a plausibility window:** default 2015 to the read time.
  Dropped and counted; the original lines are preserved in the provenance.
- **Backward time jumps:** reported. Records keep file order; the
  `ORDER BY time` read used by sensorviz sorts them.
- **Header-only and empty files:** converted to a log with a header and no
  data, flagged. A file with no header but data (variant E) is labelled
  BitTag with its format inferred from the keys.

**Provenance in every converted log.** Recorded in `info`:

- `source`: the path relative to the collection root, the SHA-256, the size
  and the variant;
- the importer version, and the conversion report;
- the original header text, verbatim;
- the parsed header fields as JSON: tag type, firmware, board, git hash,
  UUID, accelerometer configuration, start and end, hibernate windows;
- the state log, written to `states`.

**Inventory join (optional).** With `--inventory <db>`, the importer adds the
inventory's label, experiment and tag description for the UUID (or the
device string, for variant A). Nothing is inferred from file names: bird and
deployment IDs there follow several conventions, and are left to the
researchers.

### Converting the collection

**Output.** `tag-txt-import --batch` converts a tree into a parallel tree of
`.db3` files with the same relative paths, so experiment layout is preserved.
The source tree is never modified.

**Collection-level outputs:**

- a manifest of SHA-256 checksums;
- `conversion-report.csv`, one row per file: variant, type, record counts,
  every repair, and the file's group of byte-identical copies, converted
  once and linked for the others;
- a summary of totals per variant and per flag.

**Versions.** Original and re-exported (`-reformat`, `-repair`) versions of a
tag are both converted. The report pairs them by UUID, so a researcher can
choose; the importer does not decide which is right.

**Where the output lives is the researchers' choice.** The importer writes
wherever it is pointed.

- **For this project's testing,** converted logs are not kept. The batch run
  is repeatable from the collection and an importer commit.
- **What is kept:** the conversion report, as the evidence, and the trimmed,
  anonymised fixtures added to the code repository.

### Proving the SQLite writer on real experiments

The SQLite writer has not been used on a real experiment. Two checks address
that before anything is deleted.

1. **Round trip on the whole collection**, `.txt` → SQLite → `.txt`,
   described next. It runs on every converted file, all five variants.
2. **Dual-format download of the two field tags.** When the BitTag and the
   PresTag now in the field are retrieved, download each three ways:
   - `.txt`, one last time;
   - SQLite;
   - the raw Ack stream (tagclient's JSONL), archived in the private data
     repository.

   Then convert the `.txt` with `tag-txt-import`, and compare the converted
   log with the directly written `.db3`, series by series. They should agree
   exactly. This is the first real-experiment check of the SQLite writer, and
   the raw Acks keep the data reconvertible whatever is found.

From then on, archive raw Acks for every field download. Any format can then
be regenerated.

### Test fixtures

There is no BitTag or PresTag `.db3` fixture in the code repository today.
The conversion supplies them: one trimmed, anonymised BitTag log and one
PresTag log, added beside the existing fixtures so that sensorviz's loading
of these tag types is covered by its screenshot suites.

### Round-trip verification

Every converted log is turned back into text and compared with the original
`.txt` file. The original *is* the reference.

**Built into the importer, run on every conversion.** `tag-txt-import`
checks each file it converts as part of the conversion; this is not a
separate test pass. A conversion that fails the check writes no `.db3` (or,
with `--keep-failed`, writes one marked failed in `info`), and the tool exits
non-zero. A `.txt` file found years from now gets the same guarantee as the
collection does today.

**The renderer is the original writer.** The check renders with
`TextTagLogWriter`'s own formatting code, the code that wrote the
current-format files. When the text format is retired, that code moves inside
the importer as its private verification renderer rather than being deleted
(see [Removal](#removal)). For each conversion:

1. **Rebuild what the writer consumes from SQLite:**
   - a `TagLogHeader` (`TagInfo`, `Config`, calibration and state log) from
     the parsed-header JSON in `info` and the `states` table;
   - `Ack` records from the streams.
2. **Re-pack activity percentages into bucket counts.** That is exact at the
   printed precision: a count is `round(pct × period / 100)`, and the
   writer's own `count × 100 / period` reproduces the percentage.
3. **Run `writeHeader`, then `writeLog` per `Ack`,** into a string.
4. **Diff** that against the original file, after normalising line endings
   and trailing space.

**Why this beats a hand-written renderer:**

- **The formatting is the writer's own.** Default stream formatting is six
  significant digits, as with `%g`. Values that came out of this writer
  print identically when they go back in, so files from the current writer
  should match byte for byte, with no tolerance.
- **Mapping mistakes cannot cancel out.** If the importer stored `TC` as
  voltage, the writer prints it in the `V:` slot and the diff fails. Line
  order, bucket times and interleaving are checked by the code that defines
  them.
- **Metadata goes through the writer too.** The header comes from the
  structured fields only, not the verbatim header kept in the provenance, so
  a field that did not make it into structured form shows up as a
  difference.

**What the current writer cannot reproduce, and how each is handled:**

| Case | Files | Check |
| --- | --- | --- |
| Variant C1, C2, D, E | 306 | Data lines byte for byte. Header compared as a set of `key: value` fields plus the ordered state log, since header layout does not matter; for files from the current writer it should match exactly anyway |
| Variant C3/C4: the packed activity word as a fourth field on `V:` lines, written by an older writer | 51 | Data lines byte for byte, with the checker appending the re-packed word as that writer did |
| Variant B and the re-exports: older writers, different precision (`0.00`, `0.3333`) | 100 + re-exports | Line by line, epochs and keys exact, numbers within half the original's last printed digit. The header's git hash is the firmware's, so it does not identify the writer; the `NanoTag` history explains the older writers' behaviour (as with the FIVEMIN bug) but cannot pick a version per file |
| Buggy FIVEMIN (9 per record) | 69 | Rendered back through the buggy 9×7-bit layout and compared exactly; the repair is checked against the re-export where one exists |
| Variant A: RTCv4 firmware's format, from no writer in this tree | 192 | A small test-only renderer for its tab-indented lines, re-packing per-minute counts into the 32-bit hex bitmap, compared exactly, including any bits outside the 30 used |

**Allowed differences, in every case:** only the repairs listed in the
conversion report, which keeps each removed or merged line verbatim. An
unexplained difference fails the check, and so does a listed repair with no
matching difference.

**Results.**

- Each converted log records its check result in `info`: passed, or the
  first differing lines.
- `--batch` adds a per-file result column to the conversion report and exits
  non-zero if any file failed.
- Phase 2 is done only when every converted file passes, or fails with a
  reason the report already explains. Header-only files pass with empty data.
- A small check program in tagcore's option-gated `test/` directory, like
  the UIUCTag checks, runs the importer on anonymised fixtures of each
  variant, so the check itself is tested.

**What it cannot catch.**

- **For variants C, D and E:** a mistake in rebuilding `Ack`s from SQLite
  that exactly undoes a mistake in the importer. The two are separate code
  written from the schema, and the field-tag check compares the importer
  with the SQLite writer directly.
- **For variants A and B:** a shared misreading by the importer and the test
  renderer, which were written from the same understanding of the old
  format. The researchers' review in phase 3 is the independent evidence
  there.

**The field tags get the full triangle:**

| Path | Checks |
| --- | --- |
| `.txt` → SQLite, compared with the direct SQLite log | The importer against the SQLite writer |
| Direct SQLite log → `.txt`, compared with the downloaded `.txt` | The SQLite writer's data and metadata against the text writer's, on the same download |
| Raw records (`tag-log-import`) → SQLite, compared with the direct SQLite log | The two SQLite paths |

### Removal

Once phases 1–4 hold:

- **tagcore:** remove the `Text` storage format and `TextTagLogWriter` from
  tagcore's public writer API (`createTagLogWriter`, `TagLogStorageFormat`),
  so nothing can write `.txt` logs any more. The writer's formatting code
  (`txtlogs.cc`) moves into `tag-txt-import` as its private verification
  renderer, unchanged, beside the importer's parsers. Variant A's test
  renderer moves with it.
- **Tools:** drop `-f text` from `tag-dwnld`, and the `.txt` choice from
  qtmonitor's save dialog. Also remove the `txtlogs.h` includes in qtmonitor
  and qtprogram.
- **btviz:** delete `host/applications/btdataviz`, its CMake, its install and
  package entries, the Linux package test lines, and its user-manual page and
  screenshots.
- **Third-party code:** delete KissFFT, FastFIR and SOLPOS, which only btviz
  uses. Update `LICENSES/`.
- **Documentation:** update every page that mentions `.txt` logs (the
  user-manual download workflow, `tag-dwnld`, the index; host and
  architecture READMEs).
- **Decision record:** "Retire the text log format and btviz", with the
  verification evidence.

## Phases

| Phase | Work | Done when |
| --- | --- | --- |
| 0 | Stop new `.txt` logs except the dual-format download in phase 4: deprecation warnings on `tag-dwnld -f text` and qtmonitor's text choice | Warnings shown; docs say SQLite is the format |
| 1 | `tag-txt-import` for variants B, C, D, E, with the decoded-sample writer entry; provenance; unit tests on small anonymised fixtures of each variant | Fixtures convert; header-only, CRLF, duplicates and bad times are handled and reported |
| 2 | Variant A; batch mode, conversion report and manifest; convert the whole collection; add one trimmed, anonymised BitTag and one PresTag `.db3` as sensorviz fixtures | Every log file converts or is reported with a reason; every converted file round-trips through `TextTagLogWriter` (or variant A's renderer) to match its original: byte for byte for current-writer files, within printed precision for older writers, metadata field by field; sensorviz opens the fixtures |
| 3 | Researchers check converted logs against btviz on a sample of their own experiments, with whatever tools they choose | Sign-off, or issues fixed |
| 4 | Dual-format download of the two field tags; raw Acks archived; the full triangle of checks | All three comparisons agree |
| 5 | Removal; BitPresTag geometry settled from the firmware in the SQLite writer; decision record | Build and package without btviz, KissFFT, FastFIR or SOLPOS; docs check clean |

Phase 4 waits for the field tags' retrieval. Removal (phase 5) waits for
everything else.

## Open questions

- **Should `.txt` remain readable after removal,** through the importer
  alone? This proposal says yes: the importer keeps its parsers indefinitely,
  so a `.txt` file found later can still be converted.
