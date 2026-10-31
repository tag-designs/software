---
type: design
status: current
summary: Chapter 5 of the Host Software Guide - the storage model end to end (what a tag keeps in internal flash, external flash and its backup registers, how a download walks the log one page at a time and why the writer decides where it ends, the SQLite file's layout and who writes it, the other files the host reads and writes, and the offline route from a capture to a download).
---

# 5. Data and storage

This chapter follows the data: where a tag puts it, how the host gets it
back, what the host writes, and every other file the host software reads or
produces. It assumes chapters 1, 3 and 4.

## What a tag stores

A tag has three kinds of non-volatile memory, and the firmware uses each for
what it is good at:

| Memory | Holds | Why there |
| --- | --- | --- |
| **RTC backup registers** (a few dozen words, kept while the battery lasts, lost on power loss) | `pState`: the tag's state, validity magic, reset classification, counters that must survive standby | Readable and writable at any time, including from the lowest power states; the firmware's recovery journal |
| **Internal flash** (the microcontroller's; 128 or 256 KB on the L432, 1 MB on the U375, less the image) | The stored `Config`; the **state-marker log** (one entry per transition); for tags with external flash, **page headers or checkpoints** that index the external log; on CompassTag and IMUTag, the **calibration store**; on BitTag, the data itself | Always present, byte-addressable reads, erased in pages; the firmware writes it with its own flash driver |
| **External flash** (SPI NOR on the L432 families with one, 4 MB; SPI NAND on IMUTag, 256 MB) | The **data pages**: fixed-size pages of packed samples with a small header each (a 2048-byte NAND page on IMUTag, 296-byte blocks on UIUCTag, and so on, as `include/*_log_format.h` lays them out) | Capacity; powered down between writes through a load switch on most boards |

The invariants the firmware keeps, which the host relies on without
checking:

- **IDLE means an empty log.** The firmware never reports IDLE over a
  non-empty marker log; a tag holding data is FINISHED, ABORTED or
  EXCEPTION. So a tool that finds a tag IDLE may start it without erasing.
- **Only the firmware that wrote a log erases it.** `Erase` runs on the tag.
  The host never erases a tag's log by other means, and a new image is never
  flashed over a tag holding data (the flashing protocol in the root
  `AGENTS.md`, step 2).
- **A page is the unit of download**, and its layout is the family's
  business. The host asks for "the page at index N"; what N counts (a page,
  a block, a record) and how many records a page holds are decided by the
  firmware and by the matching decoder.
- **Partial writes are abandoned, not reconstructed.** A reset in the middle
  of a page leaves a page the firmware will skip; the download sees a hole,
  not garbage. The firmware's rules are in
  [`restart-recovery.md`](../../../embedded/tags/common/core/design/restart-recovery.md).

## The download

A download is a loop of `log` requests over the monitor, one page per RPC,
and a writer that decodes each page into SQLite rows. The loop is the same in
every tool and application that downloads (`tag-dwnld` in both languages,
qtmonitor's `DownloadJob`, Tag Monitor's `download.rs`):

```text
 1. Status: the tag must be FINISHED, ABORTED or EXCEPTION (optionally Stop a running one first and wait)
 2. Config: the writer is created for config.tag_type; it refuses a type it has no schema for, before touching the file
 3. Header: Config, TagInfo, every stored CalibrationConstants (index 0, 1, ... until refused),
            the whole state log (chunks until one is refused or empty); written together
 4. begin_log()                               one SQLite transaction for all the pages
 5. index = 0
    loop:
      page = Tag::data_log(index)             one RPC; an Ack whose payload is the family's log message
      if page.err == NODATA and index < count: index += 1; continue      a hole; skip it
      consumed = writer.write_log(&page)      decodes the page into rows; returns the records it held
      if consumed == 0: break                 the end of the log
      index += consumed
 6. end_log()                                 commit
```

The rule that shapes everything around it: **the writer decides how far
each page advances the index and where the log ends**, because both depend
on the family's layout, which only the decoder knows. The tool cannot ask
for page N+1 until the writer has decoded page N. That is why the writer is
a library the downloader calls synchronously, and why, when the Rust tools
needed to write SQLite, the C++ writer was wrapped as a library call rather
than run as a separate process (chapter 8). It is also why the
`Status.internal_data_count` and `external_data_count` fields drive only the
progress bar: their units differ by family, and the writer's answer is the
truth. A proposal to let the tag mark a hole explicitly (`DATASKIPPED`) is
in `docs/shared/design/proposals/`.

Downloading changes nothing on the tag, so a download can be repeated, and
a tool compares two downloads of one tag by comparing the files.

## The SQLite file

Every download is one SQLite database, conventionally `.db3`, written by
`SqliteTagLogWriter` in `libraries/tagcore/sqlitelog.cc` and the per-family
decoders in `libraries/tagcore/sqlitelog/`. It is the one format every
viewer and analysis script reads, so there is exactly one writer, and the
Rust programs use it through `tagcore_c` rather than reimplementing it
([decision 0037](../../../docs/decisions/0037-host-tagmonitor-rs.md) rejected
a Rust writer for this reason). The schema is documented for users in
[`host/docs/src/reference/sqlite-logs.md`](../../docs/src/reference/sqlite-logs.md),
which is authoritative; the writer's internal rules are in
[`sqlitelog/README.md`](../../libraries/tagcore/sqlitelog/README.md).

Every file has four common tables and then the family's sensor tables:

| Table | Holds |
| --- | --- |
| `schema_info` | `log_schema_version` and `producer` |
| `info` | Field/value metadata: `tagtype`, `uuid`, and the `TagInfo` and `Config` as protobuf JSON, so an analysis script has the whole configuration without the host code |
| `states` | The state log: epoch, state, entry reason, temperature, voltage, log sizes |
| `streams` | The catalog: for each measurement, its table, time column, value column, kind, display name, units and quantity. **Viewers discover the data from this table**, not from knowing the family |

The sensor tables follow a few shared shapes (`Voltage`, `CoreTemperature`,
`Activity`, `Pressure`, `Temperature`, each `Epoch` plus one value) and
family-specific ones (`Compass`, the IMUTag `Imu*` tables with their
elapsed-time columns and timing segments). Two conventions matter to every
reader: time is a UTC Unix epoch in seconds except for IMUTag's high-rate
tables, which use corrected elapsed microseconds from the start of
collection; and **a missing measurement is a missing row**, never a
placeholder, so gaps stay visible.

The `streams` table is also the boundary between data and display. Schema
metadata describes the data (units, quantities, labels); which streams are
shown, in what colour, on which axis, is a viewer preference and lives in
sensorViz's preference files, never in the log (the root `AGENTS.md` states
this rule).

Adding a family to the download path is a five-step recipe at the top of
`sqlitelog.cc`; its first step, registering the type in
`isTagLogStorageFormatSupported()`, is the one that fails quietly when
forgotten, so the writer refuses an unknown type before creating a file and
every downloader shows that message.

## Reading the file back

Two C++ libraries read downloads, and both are reached from Rust through
`tagcore_c`:

- **`sensordata`** loads a log for plotting without handing every sample to
  the plot: each table once into columnar `float` arrays, a min/max pyramid
  per stream, and a view query that returns an **M4 envelope** (the first,
  last, minimum and maximum sample per pixel column), so a redraw of a
  five-million-sample log is a few thousand points. Exact values come back
  from SQLite as stored `double`s when a computation must not round.
- **sensorViz's core** (`applications/sensorviz/core`) sits on it: the
  stream catalog, the display transforms (altitude from pressure, IMU
  magnitudes, the compass from calibrated magnetometer and accelerometer
  samples through `sensoranalysis`), the plot specification, the hover
  readout and the preference files. The Qt SensorViz and the Rust Tag View
  both run it, so both show the same numbers.

`dataprocessing` (C++, command line) copies a log and materialises derived
streams such as calibrated compass vectors and orientation quaternions into
it, on the Qt-free `sensoranalysis_core`
([decision 0031](../../../docs/decisions/0031-host-dataprocessing-plain-executable-without-qt.md)).

## Files the host keeps

Besides downloads, the host programs read and write these. Knowing them
answers most "where did that come from" questions.

| File | Written by | Format | Where |
| --- | --- | --- | --- |
| **Configuration file** | Tag Monitor (Save), read by Open and `tag-start --config` | `Config` as protobuf JSON, proto field names, every field written, indented ([user reference](../../docs/src/reference/config-files.md)) | wherever the user saves it |
| **Default configurations** | the firmware tree | JSON, one per variant | `embedded/proto-c/<variant>-proto-c/default-config.json` |
| **Tag Programmer's record** | Tag Programmer (both implementations) | append-only CSV, one row per programming attempt and retest, RFC 4180 quoting; `host_version` tells `tagprogram-rs` rows from `qtprogram` rows | `~/Documents/Tag Programmer/programmed-tags.csv` by default |
| **Application settings** | each Rust application | `settings.json` in the platform application-config directory (recent files, chosen base serial, record path, consent to automatic attach); the Qt apps use `QSettings` | e.g. `~/Library/Application Support/com.tagdesigns.tagmonitor/` |
| **Firmware release cache** | Tag Programmer | downloaded GitHub release archives, extracted, verified against their manifests, marked `.verified`; `releases.json` lists them | the platform data directory, `firmware/` |
| **Calibration capture** | Tag Calibrator, qtcalibrate | JSON, schema `tag-designs.qtcalibrate.calibration-capture.v1`: every sample streamed, so a session can be replayed exactly; committed fixtures drive the tests | wherever saved; fixtures in `host/docs/fixtures/qtcalibrate/` |
| **Calibration constants** | Tag Calibrator (Tag Write) | on the tag's flash, as `CalibrationConstants`; also in every download's header | the tag |
| **sensorViz preferences** | SensorViz, Tag View | JSON per tag type: visible streams, colours, axis sides, ranges, title, UTC offset, transform parameters | wherever saved; `Load Defaults` reverts |
| **Capture** | `tag-capture` (C++) | a directory: registers, internal flash, optionally SRAM, external flash, and a manifest with SHA-256 of each region | `captures/` (ignored by git; evidence, not source) |
| **Simulated logs** | `tagsim` (Python) | genuine IMUTag `.db3` files plus a truth file, through `tag-log-import` | test data |
| **Fixture logs** | committed | `.db3` files Tag View's and sensorViz's tests and the user manual's screenshots use | `host/docs/fixtures/sensorviz/` |
| **Error log** | Tag Monitor, Tag Programmer (Save Error Log) | plain text of the log pane | wherever saved |

## The offline route: capture and rebuild

When a tag will not run its firmware, the data is still in its flash. The C++
recovery library (`libraries/tagcore/recovery`) attaches with the core
halted before the first instruction, copies registers, internal flash and
optionally SRAM, then loads an SRAM-resident loader (chapter 4) to read the
external flash, and writes everything into a capture directory with a
manifest. `tag-rebuild` then feeds the capture to the **same** SQLite writer
through a capture-backed source, labelling the output "as captured"
([decision 0018](../../../docs/decisions/0018-offline-rebuild-capture-backed-source.md)):
one writer again, so an offline rebuild and a live download of the same tag
are comparable files. The identity record tells the rebuild which decoder
and loader to use. This path has no Rust port; its design is
[`swd-recovery.md`](../../libraries/tagcore/design/swd-recovery.md) and its
bench procedure [`docs/bench/capturing-a-tag.md`](../../../docs/bench/capturing-a-tag.md).

## Where to read next

- The user's view of a download file:
  [`sqlite-logs.md`](../../docs/src/reference/sqlite-logs.md).
- The writer's rules and the recipe for a new family:
  [`sqlitelog/README.md`](../../libraries/tagcore/sqlitelog/README.md), `sqlitelog.cc`.
- The Rust download tool line by line: chapter 9; the C++ writer from Rust:
  chapter 8.
- Reading for plotting: [`sensordata/README.md`](../../libraries/sensordata/README.md).
