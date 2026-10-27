---
type: proposal
status: proposed
summary: How Rust host applications should write the project's SQLite logs - an evaluation of porting sqlitelog to Rust against feeding the existing C++ writer protobuf messages, and a recommendation to start with the second through tag-log-import and a binary input format.
---

# SQLite logs from Rust applications

**Recommendation: do not port sqlitelog yet.** Let Rust applications hand the
existing C++ writer the messages they already have, through `tag-log-import`
run as a subprocess, and add a binary input format to it so the hand-off is
exact and fast. Port the writer to Rust only if a Rust application must write
logs where the C++ tools are not installed. If that happens, port it against
a differential test that runs both writers on the same input and compares the
databases, so there are never two schemas.

This document evaluates the two approaches raised:

1. **Port** the whole library to Rust.
2. **Interface:** Rust produces wire-encoded protobuf, and the C++ writer
   (`SqliteTagLogWriter`) turns it into SQLite.

## What the writer is

`host/libraries/tagcore/sqlitelog*`, about 2,600 lines of C++:

| Part | Lines | Role |
| --- | --- | --- |
| `sqlitelog.cc` | 759 | Public `SqliteTagLogWriter`, metadata tables (config, info, calibration, state history), dispatch of each `Ack` |
| `sqlitelog/schema.cc` | 427 | Tables, columns, units and stream metadata per tag |
| `sqlitelog/internal.h` | 182 | `WriterContext`, one `dump*Log()` per log type |
| `sqlitelog/imutag.cc` | 434 | IMUTag and IMUTagRaw pages |
| `sqlitelog/pressure.cc` | 212 | BitPresTag, PresTag and PresTagRaw pages |
| `sqlitelog/uiuctag.cc` | 198 | UIUCTag packed blocks (geometry from `include/uiuctag_log_format.h`) |
| `sqlitelog/bittag.cc` | 158 | BitTag and BitTagNg pages |
| `sqlitelog/compasstag.cc` | 70 | CompassTag pages |

Everything it consumes is already a protobuf message. A log is:

- a header, `TagLogHeader`: `Config`, `TagInfo`, a list of
  `CalibrationConstants` and a list of `State`;
- a sequence of `Ack`s, one per downloaded page, each passed to `writeLog()`.

The writer never needs the tag itself; `writeHeader(Tag&)` only reads those
four things and calls `writeHeader(const TagLogHeader&)`. The output schema is
documented for users in
[SQLite Log Format](../../../../docs/src/reference/sqlite-logs.md), and
sensorViz, dataprocessing and the Python tools all read it.

**The second approach already exists.** `tag-log-import` takes exactly that
header and `Ack` sequence, as protobuf JSON lines (a header object, then one
`Ack` per line), and writes the same database `tag-dwnld` writes, through the
same `SqliteTagLogWriter`. tagsim uses it to turn simulated pages into real
logs. It is installed in every host package (`tag_tools/bin`).

## Approach 1: port the writer to Rust

A Rust writer would use `rusqlite` with SQLite compiled in, and the prost
types tagcore-rs already generates. It would keep the property Tag Programmer
has: `cargo build` and nothing else.

**Cost.** About 2,600 lines of C++ to port: the schema, the metadata tables,
and nine decoders. Most of it is mechanical. The traps are in the decoders:

- UIUCTag NaN handling, where erased flash reads as a quiet NaN;
- IMUTag scaling and its exact-NaN marker;
- activity geometry, including the BitPresTag inconsistency in
  `sqlitelog/README.md`;
- time rules: block epochs that are not on a two-hour grid.

These are rules found on hardware and written down once; a second copy is a
second place to get them wrong.

**The real cost is ongoing: two writers of one format.** Every schema change,
new tag or decoder fix has to land twice, in step, or two logs of the same
download differ and nobody notices until a viewer misreads one. The recipe at
the top of `sqlitelog.cc` already lists five places to touch for a new tag;
this would make it ten, in two languages.

**What would make it safe.** A differential test that feeds the same inputs
to both writers and compares the databases table by table. The inputs would
be tagsim JSONL for every tag type, plus real downloads kept as fixtures. The
comparison would ignore only the `info` rows that name the writer. Building
that test is a good part of the work, and it has to run whenever either side
changes.

**When it pays.** Only when a Rust application must write logs on a machine
without the C++ tools: a standalone Rust downloader shipped on its own, or a
WebAssembly build. Nothing planned needs that. Tag Programmer ships inside
the host package, beside `tag-log-import`.

## Approach 2: hand the C++ writer protobuf messages

There are three ways to connect Rust to the C++ writer.

### 2a. Subprocess: `tag-log-import`, as it is (JSON lines)

The Rust application writes the header and `Ack`s as protobuf JSON lines and
runs `tag-log-import file.jsonl -o tag.db3`. tagcore-rs already prints and
parses protobuf JSON the way the C++ does (`proto.rs`), so this works today
with no C++ change.

The weaknesses are all JSON:

- `bytes` fields (UIUCTag blocks, raw IMU pages) are base64;
- floats go through text;
- an IMUTag download is large.

It is exact for every field type protobuf JSON round-trips, which is all of
them, but it is slower and bigger than it needs to be.

### 2b. Subprocess with a binary input (recommended)

Add a binary input format to `tag-log-import`. The input is a sequence of
records; each record is a one-byte kind, a varint length, and that many
bytes of one wire-encoded message:

| Kind | Message | Count |
| --- | --- | --- |
| 1 | `Config` | exactly one, first |
| 2 | `TagInfo` | exactly one |
| 3 | `CalibrationConstants` | zero or more, oldest first |
| 4 | `State` | zero or more, in order |
| 5 | `Ack` | one per page, after the header |

It starts with a short magic string and version so a wrong file is rejected.
Rust writes it with prost's `encode_length_delimited`, and C++ reads it with
`ParseFromArray`. Neither side interprets the other's data. Wire encoding is
exact by construction (bytes are bytes, floats are bits), it is compact, and
the header needs no new `.proto` message, so nothing in `proto/` or the
firmware's nanopb build changes.

The same file is a useful artifact on its own: an exact, replayable record of
a download, from which the SQLite log can be rebuilt with a newer writer. It
could be kept beside the `.db3`, as `tag-rebuild` keeps captures.

**Cost:** about 150 lines in `tag-log-import.cc` (a record reader and a
`--format` option), a Rust writer of about 50 lines in tagcore-rs, and one
test: write the same download as JSONL and as binary, import both, and
compare the databases.

**Limits:**
- The Rust application depends on `tag-log-import` being installed. Inside
  the host package it is; a developer running `cargo run` needs it built or on
  `PATH`, which tagsim already handles with `TAG_LOG_IMPORT_BIN`.
- Progress and errors cross a process boundary. The tool's exit status and
  stderr carry the result, and a Rust caller can stream records into its
  stdin as pages arrive, instead of writing a file first, to keep the writer
  in step with a long download.

### 2c. Link the C++ writer into Rust (not recommended)

This means a C ABI over `SqliteTagLogWriter`, such as
`sqlitelog_open(path, config_bytes)`, `sqlitelog_header(...)` and
`sqlitelog_write_ack(bytes)`, linked from Rust.

It avoids a subprocess, but the Rust build would then need the whole C++
stack: libtagcore, the C++ protobuf runtime with abseil, and SQLite, all from
vcpkg, on three platforms. It would link them with the Rust toolchain. That
gives up `cargo build` alone, puts a second SQLite in any Rust program that
also uses `rusqlite`, and makes Windows linking of static vcpkg libraries
into a Rust binary a maintenance job of its own. A subprocess gets the same
result with none of that.

## Comparison

| | Port (1) | JSONL subprocess (2a) | Binary subprocess (2b) | Linked C ABI (2c) |
| --- | --- | --- | --- | --- |
| Works today | no | **yes** | small change | no |
| One writer, one schema | no: needs a differential test forever | **yes** | **yes** | **yes** |
| Exact transfer | n/a | yes, through text | **yes, bytes** | yes |
| Speed and size | best | worst (base64, text floats) | **good** | best |
| Rust build stays cargo-only | **yes** | **yes** | **yes** | no |
| Needs C++ tools at run time | **no** | yes | yes | no (linked in) |
| Effort | about 2,600 lines plus the test | none | about 200 lines plus a test | ABI plus a cross-platform build |

## Plan

1. **Now, if a Rust application needs logs before anything else is built:**
   use 2a, with JSONL from tagcore-rs and `tag-log-import`, unchanged.
2. **Next:** add the binary format (2b) to `tag-log-import`, the writer
   helper to tagcore-rs, and the JSONL-against-binary test. Rust applications
   use it from then on.
3. **Only if a Rust application has to write logs without the C++ tools:**
   port (1), starting with the differential test, then the decoders one tag
   at a time, each passing against the C++ writer before the next.

## Questions for the owner

- Should the binary download record be kept beside each `.db3` as the
  replayable source of truth, or only used in transit?
- Does any planned Rust application need to run without the host package?
  That is the only thing that would justify the port.
