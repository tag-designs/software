---
type: decision
status: accepted
summary: An offline rebuild is a capture-backed data source feeding the unchanged SQLite writer and per-tag decoders, its output labelled "as captured".
---

# 0018. Offline rebuild through a capture-backed source for the unchanged SQLite writer

Date: 2026-10-01

Item 1 of the plan agreed on 2026-10-01 for rebuilding a download from an SWD capture. Built on 2026-10-02 as `tag-rebuild`; the consequences section is its dated implementation record, validated on all five distributed targets by 2026-10-03. Cut verbatim from [Offline Log Reconstruction](../investigations/2026-10-offline-log-reconstruction.md), "Decisions and plan", which holds the fw-v0.0.3 analysis behind it.

## Context

Agreed on 2026-10-01. Items 1 and 2 need no firmware change, and so serve every
tag already deployed with this release. Items 3-5 make future releases
self-describing.

## Decision

The offline path is a **capture-backed data source** that sits where the live
tag sits. The SQLite writer (`sqlitelog.cc`) and its per-tag decoders stay
exactly as they are, so the schema, timestamp rules and conversions remain
single-sourced on the host.

- **It must reconstruct the whole file, not only the data rows.** A SQLite
  download also carries:
  - `info` (the `TagInfo` JSON and its individual fields);
  - the `Config` JSON;
  - the `Calibration` table (one row per stored calibration slot);
  - the `State` history.

  Each is rebuilt from the capture as described under
  [Common header inputs](../investigations/2026-10-offline-log-reconstruction.md#common-header-inputs). `TagInfo` comes from the
  image strings and chip registers, and later from the identity record of
  item 4. `Config` comes from a host re-implementation of the family's
  `readConfig()`. Calibration comes from `calConstants`, and state history
  from `sEpoch[]`.
- **Interface.** `writeHeader()` takes a `Tag &` and calls `GetConfig`,
  `GetTagInfo`, `ReadCalibration` and `GetStateLog`; `writeLog()` takes data-log
  `Ack`s. The shim supplies exactly those, produced from capture bytes instead
  of monitor RPCs. That needs an interface the writer depends on, implemented
  by both the live `Tag` and a `CaptureSource`. The same seam later lets the
  Python binding feed a capture to the writer.
- **Per-tag data decoders** synthesise the tag's data-log protobuf from
  internal headers and external pages, reproducing `data_logAck()`: the same
  float32 conversions, the same truncation rules, and the quirks listed under
  [Per tag](../investigations/2026-10-offline-log-reconstruction.md#per-tag). `legacy_bittag_rescue` and `uiuctag_end_to_end_check`
  are the precedents.
- **Layout knowledge** comes from the release source for fw-v0.0.3 images, and
  from the package descriptor of item 3 for later releases.
- **The output is labelled "as captured".** It differs legitimately from a live
  download: there is no stop marker, and reset recovery did not run.

## Consequences

Implemented 2026-10-02 in `host/libraries/tagcore/recovery/capturesource.{h,cc}`
and the `tag-rebuild` command. The interface came out push-style rather than as
an interface the writer pulls from:

- `TagLogHeader` (`taglogwriter.h`) holds Config, TagInfo, the calibration
  slots and the state history. `readTagLogHeader()` gathers it from a live tag
  with the same monitor calls the writer made before, and
  `SqliteTagLogWriter::writeHeader(const TagLogHeader &)` writes it. The live
  path now goes through both, and a full `.dump` of a live download was
  identical before and after the change. Only the SQLite writer takes a pushed
  header; recovery does not produce text logs.
- `CaptureSource` builds the `TagLogHeader` from the capture. The common parts
  (TagInfo, calibration, `sEpoch[]`, session facts) follow the shared firmware
  code, laid out by the identity record of item 4. A per-family
  `CaptureDecoder`, chosen by the record's `decoder` string, supplies
  `readConfig()`, `externalFlashSize()`, the data-log count and
  `data_logAck()`. There are five, one for each distributed target:
  - `imutag`: NAND checkpoints;
  - `prestag`: converted samples, NOR;
  - `compasstag`: blocks with activity words, NOR;
  - `uiuctag`: time-indexed slots, NOR, served raw, with the erased-slot rule
    taken from the shared `uiuctag_log_format.h`;
  - `bittag`: internal records only, 30 per Ack.

  BitTag's `readConfig()` reports the image's built-in default Config in IDLE
  or TEST. The decoder does the same, through the identity record's
  `default_config` entry. Other families, and images without an identity
  record (fw-v0.0.3), are refused.
- A capture is checked before anything is written. Each file must match the
  SHA-256 its manifest records and must not be marked failed, and a NOR image
  must be the size the record gives. Each region's `layout_version` and
  record size must be one the decoder knows. A rebuild that fails part way
  removes its output.
- The data log is walked as `tag-dwnld` walks it: a NODATA index below the
  count is a hole and is skipped.
- Provenance goes into `info`: `source` = `capture`, `capture_dir` and
  `captured_at`, through `SqliteTagLogWriter::writeInfo()`.

**Validation.** The IMUTagNandBmp581 reference pair is
`captures/2026-10-02-imutag-nand-bmp581/d1-reference`, a 60 s run at 100 Hz:
52 pages and 7800 samples. A table-by-table `.dump` of the rebuild against the
live `tag-dwnld --stop -f sqlite` gave:

- every data table, `streams`, `schema_info`, `Calibration` and `states`
  identical;
- `info` differing only by the three provenance rows.

PresTag, on the bench unit `20333050364150040063005F`
(`captures/2026-10-02-prestag-d1`), gave the same result for:
- a 90 s run, with one partial page of 3 samples;
- a 1 s run of 12 pages, several of them partial, because each attach starts
  a new page;
- `regression-1`, a 1 s run captured mid-run and again after the stop.

CompassTagAT25 (`captures/2026-10-02-compasstag-d1`) gave the same result
for 400 s runs, before and after its A7 fix.

BitTag V6 (`captures/2026-10-03-bittag-d1`) passed as well:
- a 600 s run at one bit per second (10 records), table for table;
- the log found on it (ABORTED, no records), header only, matching
  `tag-dwnld`, which writes the header and stops when the count is zero;
- in IDLE, the rebuilt Config equal to what `tag-info` reads live: the
  image's default, not the stored configuration. `tag-dwnld` refuses to
  download in IDLE, so `tag-info` is the reference there.

Its image (`8eceeb5`) predates the B2 fix, so its session facts hold no RTC
offset. Live, `infoAck()` then reads the RV3028; a capture cannot, so the
rebuild leaves `ppm_clock_error` unset. `tag_rebuild_check.py compare`
reports that field as a note rather than a failure. Recording the EEOffset
in the capture (D3) would close it.

UIUCTag (`captures/2026-10-03-uiuctag-d1`), a 20 min run, gave the same
result. Its slots are indexed by time: the slot that fell inside the
capture's halt is missing, and the later samples keep their own times. So
A7 cannot occur on UIUCTag.

A mid-run capture rebuilds to an exact prefix of the final download: 100 and
75 samples in two runs, ending at the capture instant. The partial page is in
external flash sample by sample.

The mid-run captures also exposed a capture-path fault. The SWD session left
`DHCSR.C_MASKINTS` set, which stalled the next monitor attach. It is fixed;
see `host/libraries/tagcore/design/swd-recovery.md`, "Attaching without
booting the firmware".

**Gaps.**

- The identity record carries the NAND erase unit but not the logical block
  count that `externalFlashSize()` multiplies by: 2008 for the GD5F2GM7RE, a
  constant in the decoder. It should become a record number.
- Live, `infoAck()` falls back to reading the RV3028 offset over I2C when the
  session facts are invalid. A capture has no such fallback, so the rebuild
  leaves `ppm_clock_error` unset in that case.
