# Offline Log Reconstruction

Status: analysis, with decisions agreed on 2026-10-01 (see
[Decisions and plan](#decisions-and-plan)). Item 1 is implemented for
IMUTagNandBmp581, PresTag and CompassTag (`tag-rebuild`, 2026-10-02; see
[Implementation status](#implementation-status)); the other families are not
yet. It answers one
question for every tag in the `fw-v0.0.3` firmware package (`d16a930f`):
**from a raw capture of a tag plus the released firmware package, can we build
the same SQLite file that `tag-dwnld -f sqlite` would have produced -- and if
not, what is missing?**

The method was reading, not running. The release source was checked out at
`d16a930f`. Each tag's download path was traced from the monitor request
through its `datalog.c` (`data_logAck()`, which builds the data-log protobuf
from internal and external flash) to the host decoder in
`host/libraries/tagcore/sqlitelog/`. The package ELFs were inspected with
`nm`/`readelf`. No reconstruction has yet been carried out, so treat
"bit-identical" below as a property of the code paths, not a test result.

Related: [Field Data Extraction](field-data-extraction.md) (why raw dumps are
not self-describing; the session superblock proposal) and
[SWD Capture and Recovery Library](../host/libraries/tagcore/design/swd-recovery.md)
(the capture tool these inputs come from).

## The inputs

A **capture** here means what the SWD capture library can take from a tag
without booting it:
- all 32 backup registers (the `pState` mirror);
- the MCU unique ID and flash-size register, option bytes, OTP;
- the whole internal flash: image, `.persistent`, configuration, calibration
  and NAND-map pages;
- a raw dump of the whole external flash.

The **firmware package** (`firmware 4/<Tag>/`) is the `.elf`, `.map`, `.list`,
`.bin`, `.hex`, `.dmp` and build manifest for each of BitTag, CompassTagAT25,
IMUTagNandBmp581, PresTag and UIUCTag. The ELFs carry a full symbol table but
**no DWARF type information**. They are built with **short enums**
(`Tag_ABI_enum_size: small`), so a stored struct with an enum member has a
different layout from what a quick read of the source suggests.

The SQLite writer (`sqlitelog.cc`) needs five things from a live tag:
1. `Config`;
2. `TagInfo`;
3. the calibration history;
4. the state-transition log;
5. the stream of data-log ACKs, one per stored page or record.

Each is classified below by where it can come from offline:

| Class | Source |
| --- | --- |
| **A** | the capture alone |
| **B** | the package's ELF symbols or `.map` (addresses, region sizes) |
| **C** | the source at the release commit only (struct layouts, compile-time constants, conversion formulas, decode rules) |
| **D** | stored nowhere offline |

## Answer

| Tag | Capture + package + source | Capture + package alone | Gaps that no amount of source closes (D) |
| --- | --- | --- | --- |
| BitTag | Yes, exact | Nearly: one 16-byte record and two scale factors | ECC status of a record |
| PresTag | Yes, exact | No | ECC status of a header |
| UIUCTag | Yes, bit-identical | Nearly: two record layouts and a few rules | ECC status of a checkpoint |
| CompassTagAT25 | Yes, exact | No | ECC status of a header |
| IMUTagNandBmp581 | Yes, except corrected sample times | No | `ppm_clock_error` (sample `ElapsedUs` only; epochs are corrected on the tag); NAND ECC outcome |

The common thread:
- **Every addresses question is answered by the package.** All the needed
  regions have symbols with sizes in every ELF: `vddHeader`, `sconfig`,
  `sEpoch`, `calConstants`, `tag_default_config`, `__persistent_start__/_end__`,
  and the calibration, config and NAND-map section bounds.
- **Every layout question is answered only by the source.** No struct, sample
  count, scale factor or end-of-log rule is recorded anywhere a host can read
  without it.
- **Since the build is reproducible and the manifest names the commit, source
  at the release commit is always available in principle.** But decoding then
  depends on the right checkout, which is the dependency Field Data Extraction
  set out to remove.

## Common header inputs

| Input | Live source | Class |
| --- | --- | --- |
| `uuid` | `UID_BASE`, printed UID[2]..UID[0] in uppercase hex (`monitor.c:757`) | A |
| `intflashsz` | Flash-size register, in KB despite the proto comment (`monitor.c:767`) | A |
| Config (downloadable states FINISHED/ABORTED/EXCEPTION) | Per-family `readConfig()` maps a raw `t_storedconfig` (`sconfig`) into protobuf. The mapping is lossy and does arithmetic: BitTag scales thresholds by its `Sens[]` table; IMUTag drops start/stop. | B + C |
| `tag_type` | Compile-time `TAG_TYPE`; recoverable by decoding `tag_default_config` (a nanopb blob in rodata) | B + proto |
| `board_desc`, `gitrepo`, `githash`, `build_time`, `source_path`, `firmware` | String literals in the image. `InfoStrings` was optimised away and has no symbol, so nothing says which literal is which field. | C (or B by disassembling `infoAck`) |
| `extflashsz` | `sector_size * sector_count` from a const storage descriptor; no usable symbol | C (A from the dump length) |
| `qtmonitor_min_version`, `accelconstant`, `magconstant` | Compile-time macros | C |
| **`ppm_clock_error`** | **Read live from the RV3028 RTC's EEOffset EEPROM over I2C** in `infoAck()` (`tagRtcRefreshClockCorrection()`, `rtc_rv3028.c:224-260`). All five images use the RV3028. | **D** |
| Calibration | `calConstants`: `{int32 timestamp; 13 floats}` slots, 56 B x 36 on L4, 64 B x 32 on U3 (padded); the first slot with timestamp -1 ends it | B + C |
| State log | `sEpoch[]` of `t_StateMarker`: 24 B x 24 on L4, 32 B x 20 on U3. Reading stops at epoch -1, an ECC fault, an out-of-range state or reason, or the array end. | B + C |
| Status counts (`pages`, `external_blocks`) | Backup registers. **The `BackupState` layout is per family**: `families/*/inc/persistent.h` overrides the common header, and UIUCTag uses the BitPresTag family's. On U3 it is `TAMP->BKP0R`. | A + C |

`ppm_clock_error` matters for IMUTag only, and only for the sample timeline.
- **Epochs are already right.** IMUTagNandBmp581 builds with
  `IMUTAG_USE_STM32_RTC_SMOOTH_CALIBRATION=1`. When the RTC is set
  (`time.c:388`), the firmware loads the RV3028 EEOffset into the STM32 RTC's
  smooth calibration (`RTC_CALR`), so the RTC calendar, and every page epoch,
  is corrected.
- **The sample grid is deliberately not.** The LSM6DSV16X trigger runs from the
  raw, uncompensated 32.768 kHz CLKOUT through the LPTIM, to avoid the
  RV3028's correction-pulse jitter. The host places samples on that raw grid
  and ties it to the corrected RTC at segment anchors, scaling elapsed time by
  `1 + ppm/1e6` in between (`sqlitelog.cc:570-590`). See
  `families/IMUTag/design/jitter-free-sampling-timing-reconstruction.md`.

So `ppm_clock_error` affects `ElapsedUs`, `StartElapsedUs` and
`ImuSegment.CorrectionPpm`, but not epochs, `RawElapsedUs` or sample values.
For the other tags it appears only in the info JSON.

That timing design already says to store the factory EEOffset with the log.
This release does not: the value leaves the tag only live, read over I2C into
`TagInfo` at download time. EEOffset is a per-chip constant, and it will be
stored with the stored configuration, which is written when the tag is started
(item 5 of [Decisions and plan](#decisions-and-plan)).

**An offline file will legitimately differ from a live one in two ways.**
- `tag-dwnld` stops a RUNNING tag before downloading. That adds a final marker
  with the download-time epoch, voltage and temperature, and flushes samples
  held in RAM. A capture records the tag as it was found.
- The attach under reset lets reset recovery run before the live download, and
  that can recount cursors or add markers. The capture does not boot the
  firmware, so none of this happens.

The offline tool should label its output "as captured" rather than imitate
these.

## Per tag

### BitTag

- **Path.** `BitTag/src/datalog.c:81-115` (tag-local). Each ACK carries up to
  30 records.
- **Layout.** Internal flash only; there is no external flash. Each record is a
  16-byte `t_DataHeader {int32 epoch; int16 temp10; uint16 vdd100; uint64
  activity}`. They run from `vddHeader` (0x08008A68 in this image) to
  `__persistent_end__`, about 14,169 records. The log ends at the first epoch
  of 0xFFFFFFFF; `pState->pages` is not used to find it.
- **Firmware transformation.** Only `temp10 * 0.1f` and `vdd100 * 0.01f` (both
  float32). The activity bits are packed at write time and passed through.
- **Host.** `sqlitelog/bittag.cc` expands the activity using
  `Config.bittag_log` (`sconfig` byte 7).
- **Prior art.** `legacy_bittag_rescue.cc:29-193` already performs this exact
  conversion over SWD. It does not apply to this release:
  - it hard-codes `vddHeader` at 0x08007A68, which in fw-v0.0.3 lies inside
    `.text`, 4 KB too low;
  - it only runs for `BITTAG`, while this image reports `BITTAG_LE`.

  Taking the address from the ELF and accepting `BITTAG_LE` makes it the
  offline decoder.

### PresTag

- **Path.** `families/PresTag/src/datalog.c:313-410`. The tag-local `*.c-back`
  and `*-stash` files do not build. `PRESTAG_RAW_LOG` is 0, so the payload is
  `PresTagLog`.
- **Internal headers.** Each is an 8-byte `{int32 epoch; uint16 vdd100[2]}`,
  from `vddHeader` (0x08009260) to `__persistent_end__`, about 28,084 of them.
- **Pairing.** Header *i* owns external bytes `[i*240, i*240+240)`: 60 packed
  `{int16 pressure, int16 temperature}` samples.
- **End of log.** The first epoch of -1; within a page, the first pressure of
  -1.
- **Firmware transformation.** `pressure = raw/16.0f` and
  `temperature = raw/100.0f` (`lps27.c:118-131`), plus `vdd100 * 0.01f`.
- **Host.** `sqlitelog/pressure.cc` times samples as
  `epoch + j * Config.period`. `Config.period` comes from `sconfig.lps_period`.
- **Quirk worth reproducing.** When external flash fills, the last header owns
  a 16-sample page at offset 4194240. That page fails the `+240 <= size` check
  (`datalog.c:374`), so it is never downloaded. An exact rebuild must drop it
  too; a better one could recover it.

### UIUCTag

- **Path.** `UIUCTag/src/datalog.c:413-465` (tag-local; it replaces the
  BitPresTag family file).
- **Layout.**
  - Checkpoints are 8-byte `{int32 epoch; uint16 vdd100; uint16
    extern_log_block}` entries in `vddHeader` (0x0800BA60).
  - External blocks are 288 bytes: 24 x 12-byte
    `{float pressure, float temperature, uint32 activity}`
    (`include/uiuctag_log_format.h`).
  - Missing pressure or temperature is any NaN; missing activity is
    0xFFFFFFFF.
- **Firmware transformation.** Only `vdd100 * 0.01f`, and trimming of
  trailing erased slots. The floats are sent byte for byte.
- **Index bound.** `pState->pages` (word 4 of the *BitPresTag* `BackupState`).
- **Prior art.** `host/libraries/tagcore/test/uiuctag_end_to_end_check.cc:56-90`
  already takes `(epoch, vdd100, 288-byte image)`, applies the trim, builds the
  ACK and feeds the real writer. Offline reconstruction is that loop with
  capture input.
- **Choice for the tool.** Bound by `pState->pages` to match the tag, or scan
  to the first epoch of -1 to recover data when the backup domain was lost.

### CompassTagAT25

- **Path.** `families/CompassTag/src/datalog.c:311-384`.
- **Layout.**
  - 8-byte headers `{int32 epoch; uint16 vdd100; uint16 temp10}` in
    `vddHeader` (0x0800BA58).
  - Header *i* owns 380 external bytes at `380*i`: 10 blocks, each
    `{RawSensorData[3] (6 x int16); uint16 activity}`.
- **Firmware transformation.**
  - `a = raw * 0.976f` mg and `m = raw * 0.04f` uT.
  - Activity is 5-bit fields, each `* 100.0f / 30`.
  - A block whose activity word is still 0xFFFF is skipped.
  - Axis orientation is applied before storage; no calibration is applied
    anywhere on the tag.
- **Host.** `sqlitelog/compasstag.cc` times rows as `epoch + k*30`, where *k*
  counts only *emitted* rows. A skipped block therefore pulls later rows
  earlier, and offline must reproduce that.
- **Calibration.** Stored in `calConstants` (0x0800B000) and copied to the
  `Calibration` table as JSON; sensorviz applies it.

### IMUTagNandBmp581

- **Path.** `families/IMUTag/src/datalog.c:917`.
  - `readCheckpointForPage()` (`:548`) finds the governing checkpoint, and
    `physical = checkpoint.external_page_physical_next + (index -
    logical_next)`.
  - The NAND map is used when a checkpoint is written, not at download.
  - The page is read with the GD5F's **on-die ECC enabled** and accepted only
    if ECC is OK or corrected and the epoch is not -1.
- **Layout.**
  - 2048-byte pages: an 8-byte header plus 15 superframes of 136 bytes.
  - Checkpoints are 16-byte rows in `vddHeader` (0x08021280).
  - The NAND map is `uint16[2048]` at 0x080FF000, with no header, magic or
    CRC.
  - The firmware never writes the spare area.
- **`sconfig` layout depends on the short-enum ABI:** start@0, stop@4,
  start_delay@8, odr (u16)@12, accel_rng (u8)@14, gyro_rng (u8)@15. Read with
  32-bit enums in mind, the source gives the wrong offsets.
- **Firmware transformation.** Only the 1/1024 s millis and flag bits, and
  `rawtemp * 0.01`. The 2048-byte page goes through raw.
- **Host.** `sqlitelog/imutag.cc` decodes the page bytes, using `Config.lsm6`
  (ODR, ranges) and `ppm_clock_error`.
- **Two D gaps.**
  - `ppm_clock_error`, for sample `ElapsedUs` only; epochs come from the
    smooth-calibrated RTC and are correct (above).
  - The NAND ECC outcome. The GD5F's on-die ECC algorithm is not in the
    source. So a raw, uncorrected dump can neither correct bit flips the tag
    would have fixed nor identify pages it would have dropped.

  This revises Field Data Extraction's "raw reads for NAND" advice: the capture
  must also read each used page **through** on-die ECC and record its ECC
  status.

## Firmware defects found

These came out of the reading. They are not fixed here; each is a separate
firmware change, with the qualification that implies. They are scheduled, with
fixes and verification, in
[Tag Firmware: Next Release TODO](../embedded/tags/design/next-release-todo.md). An offline rebuild
reproduces each of them faithfully.

- **CompassTag page stride (data corruption).** `external_blocks` counts
  16-bit words: the write address is `external_blocks * 2`
  (`families/CompassTag/src/datalog.c:257`), and a page is
  `sizeof(t_DataLog)/2` = 190 words. But `Running(T_INIT)`
  (`state_run.c:62`) and `restoreLog()` (`datalog.c:231`) set it to
  `pages * 30`. Any re-entry with pages already logged (hibernation end,
  brownout restart, restore-based recovery) therefore writes new samples over
  earlier pages, and on NOR that corrupts them. Unchanged at `HEAD`.
- **CompassTag sub-zero temperature.** `t_DataHeader.temp10` is `uint16_t`
  (`datalog.h:47`) but stores a signed value, so below 0 °C the core
  temperature reads about 6553 °C.
- **PresTag last partial page** is never downloaded (above).
- **The BitTag rescue path** hard-codes an address from older firmware and
  rejects `BITTAG_LE` (above).

## Decisions and plan

Agreed on 2026-10-01. Items 1 and 2 need no firmware change, and so serve every
tag already deployed with this release. Items 3-5 make future releases
self-describing.

### 1. A shim between the SQLite writer and SWD extraction

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
  [Common header inputs](#common-header-inputs). `TagInfo` comes from the
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
  [Per tag](#per-tag). `legacy_bittag_rescue` and `uiuctag_end_to_end_check`
  are the precedents.
- **Layout knowledge** comes from the release source for fw-v0.0.3 images, and
  from the package descriptor of item 3 for later releases.
- **The output is labelled "as captured".** It differs legitimately from a live
  download: there is no stop marker, and reset recovery did not run.

#### Implementation status

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
  `data_logAck()`. `imutag` (NAND checkpoints), `prestag` (converted
  samples, NOR) and `compasstag` (blocks with activity words, NOR) exist so
  far. Other families, and images without an identity
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

#### Keeping the rebuild in sync with the firmware

The decoders are a second implementation of each family's monitor handlers.
Nothing makes them follow the firmware automatically, so each kind of drift
has its own guard.

| What can change | Guard | Catches it |
| --- | --- | --- |
| A struct the decoder reads: stored config, state marker, data header or checkpoint, calibration slot, session facts | `_Static_assert` on every offset and size the decoder uses, next to the type, naming `capturesource.cc` | at firmware build time; the images are byte-identical with or without the asserts (`.list` compared, PresTag and IMUTagNandBmp581) |
| The same change, made deliberately | bump the region's `layout_version` in the identity record; the host refuses a version or record size it does not know | at rebuild time, as a refusal instead of a misread |
| The download logic in `data_logAck()`: checkpoint search, flag masking, conversions, page termination, holes | `embedded/tools/tag_rebuild_check.py run` on hardware: a mid-run and a final capture, rebuilt and compared table by table with a live download | at release qualification |
| A host decoder change | `tag_rebuild_check.py compare` over the stored reference pairs | before committing the host change |
| The SQLite writer | none needed: live downloads and rebuilds use the same writer and the same `TagLogHeader` | by construction |
| A family with no decoder | `tag-rebuild` refuses it by name | always |

What remains unguarded:

- **Constants the record does not carry.** The GD5F logical block count
  (2008) is the one so far. It should move into the record's numbers.
- **Algorithm drift between hardware checks.** A change to `data_logAck()`
  that keeps every struct is caught only when the hardware check runs. The
  remedy without hardware is a differential test: compile the real
  `data_logAck()` against stubs, as `families/PresTag/test/datalog_sim.c`
  already does; dump the simulated flash as a capture directory with the
  encoded Acks; and require `CaptureSource` to produce the same Acks byte for
  byte. Not built yet.

The rule for a firmware change:
- **A struct listed above:** bump its layout version and update the decoder.
- **`data_logAck()` or `readConfig()`:** update the decoder and run the
  hardware check.
- **A host decoder:** run `compare` on every reference pair.

The reference pairs are kept outside the repository, in `captures/`, at 1 to
5 MB each.

### 2. Close the gaps in the capture

- **For tags in the field now, read the RV3028's EEOffset over I2C.** A small
  SRAM-resident routine, run through the capture library's SRAM-call layer
  (the same mechanism as the flash loaders), can do this without booting the
  tag. Item 5 makes this unnecessary for future releases.
- **For NAND, read each used page through on-die ECC and record its status**,
  as well as the raw page.
- **For internal flash, record which double-words raise ECC faults.** The
  firmware truncates the log at the first one.

### 3. A layout descriptor generated at build time and shipped in the package

It holds:
- the symbol addresses and section bounds already in the ELF;
- for every stored struct, `sizeof` and member offsets, emitted by a
  build-time C program compiled with the target's flags. The ELF has no DWARF,
  and the short-enum ABI must be captured;
- compile-time constants: samples per page, stride, cadence, sample period;
- scale factors, and whether each is a multiply or a divide;
- end-of-log and missing-value rules, as named rule IDs;
- the per-family `BackupState` word map;
- the `.proto` files, or a `FileDescriptorSet`;
- the decoder name.

With it, capture plus package is enough for every tag, and the source checkout
is no longer needed.

### 4. A tag identity record immediately after the interrupt vectors

A const, versioned record holding the key compile-time constants that
identify a tag's type, hardware and software: everything the tag-info call
reports, plus what a downloader needs to choose its loader and decoder. It is
the first thing a capture reads after identifying the processor.

**Where it is.** It goes in its own linker section placed directly after
`.vectors`. The SWD session already identifies the processor from
`DBGMCU_IDCODE`, and the vector table has a fixed size per MCU. In every
fw-v0.0.3 image it is:
- `0x1A0` bytes on the STM32L432 tags, so the record is at `0x080001A0`;
- `0x240` bytes on the STM32U375, so the record is at `0x08000240`.

So the host reads the record at a known address for that MCU: one read, no
search. The address belongs in the per-MCU table (`swdmcu`). If the magic is
not found there -- every image released before the record exists -- the host
falls back to identification by image hash or strings, as for fw-v0.0.3.

**Contents.** It starts with a magic word, a format version and its size, so
it can grow without breaking older readers. Then:

- **What the tag is:**
  - `tag_type`;
  - the target name (e.g. `PresTag`) and family;
  - `board_desc` and a board hardware revision;
  - the external flash part (JEDEC ID) and its geometry;
  - the RTC part;
  - the name of the loader that reads its external flash
    (e.g. `AT25XE_PresTagv3`);
  - the name of the data decoder, with its layout version.
- **What software it runs:**
  - `firmware` (`FIRMWARE_STRING`);
  - `gitrepo`, `githash`, the commit date (`build_time`) and `source_path`;
  - the version of the record format itself.
- **The rest of what the tag-info call reports:** `qtmonitor_min_version`,
  `accelconstant` and `magconstant`. `infoAck()` reads all of these from the
  record rather than from scattered literals, so live and offline report the
  same values from one source.
- **A region table and decoding constants**, described in the next
  subsection.

#### Region table and decoding constants

Every address and size below is already available from the `.map` or the ELF,
and every layout fact from the source. Putting them in the record means a
host needs neither: one read at a fixed address tells it where everything is
and how big each record is. That holds even for an image whose package has
been lost.

**Format.** Entries are `{u16 id; u16 length; value}` after an 8-byte header
(magic, format version, total size), ending with an end entry. Readers skip ids
they do not know. So a family can add entries, and the format can grow, without
breaking older hosts. Values are little-endian `u32`s, or NUL-terminated strings
for the identity fields.

As built (format version 1, `common/core/src/tag_identity.c`), two details
differ from the first draft:
- **There is no CRC.** A C initializer cannot compute one. The magic, the size
  field and the end entry identify a well-formed record, and the image's
  SHA-256 covers its bytes.
- **Regions are given as start and end addresses, not sizes.** Each address is
  a link-time constant, while the difference of two symbols is not. The data
  headers' end is given as 0, meaning "to the end of the persistent region".

The reader is `embedded/tools/decode_tag_identity.py`.

**Regions.** Each region entry gives `{address, size, record_size,
record_count, layout_version}`:

| Region | What it is (fw-v0.0.3 name) | Why a decoder wants it |
| --- | --- | --- |
| Persistent region | `__persistent_start__`..`__persistent_end__` | Bounds everything the log scan may read; the erase unit is the MCU page size |
| State markers | `sEpoch[]`, `t_StateMarker` | Start of the states; the record size differs between L4 (24 B) and U3 (32 B) |
| Stored configuration | `sconfig`, `t_storedconfig` | Location of the config; whether it has its own page; its layout version, which captures the short-enum ABI |
| Default configuration | `tag_default_config`, nanopb blob and length | `tag_type` and defaults, decodable with the proto schema alone |
| Data headers | `vddHeader[]` | Start, record size, and the real end: the array runs on to `__persistent_end__`, not to its declared length |
| Calibration | `calConstants[]` | Slot size and count, which differ between L4 (56 B x 36) and U3 (64 B x 32) |
| NAND map | `gd5fLogicalBlockMap` | Address, entry size, logical and physical block counts |
| Scratchpad | `0x2003E000`, 8 KB (U375 builds with `TAG_SCRATCHPAD`) | Where retained diagnostics are, and whether this image has them |
| U3 monitor mailbox | the shared-memory block | Lets a host recognise the monitor path without trying it |

**Backup registers.**
- The base address: `RTC_BKP0R` or `TAMP_BKP0R`.
- The word count.
- The word index of each `BackupState` field the decoders use: `valid`,
  `state`, `pages`, `external_blocks`, `resetCause`.
- `BACKUP_STATE_VALID_MAGIC`.

This replaces reading the per-family `persistent.h`, the source of the
UIUCTag/BitPresTag surprise.

**External flash.**
- The part's JEDEC ID and total size.
- The program-page size, the erase-unit size, and the spare-area size (NAND).
- The data region's base, and how a header maps to it: the page stride for the
  fixed-stride tags (240 B PresTag, 288 B UIUCTag, 380 B CompassTag), or
  "via checkpoint" for IMUTag.

**Data-format constants** for the tag's own decoder:
- the header record size and the sample record size;
- samples per page or block;
- the nominal sample period, or where it is read from in the stored
  configuration;
- the sub-second tick rate (1024 Hz on IMUTag);
- the scale factors, as IEEE floats (0.01 for `vdd100`, 1/16 for LPS27
  pressure, 0.976 mg and 0.04 uT for CompassTag);
- the erased-value conventions (epoch `-1`, `0xFFFF` activity, NaN samples).

These let a generic decoder handle the simple tags, and let a tag-specific
decoder check that it is reading the layout it was written for.

**Validation limits:**
- `_TagState_MAX` and `_State_Event_MAX`, which bound a valid state marker;
- the proto schema version, or a hash of `tag.proto` and `tagdata.proto`, so
  the host can pick matching `.proto` files.

**Build identity beyond the commit:**
- a digest of the compile-time option set (`UDEFS`, or the tag's `project.mk`
  and `custom.h`);
- named flags for the options that change what is stored or retained, such as
  `TAG_SCRATCHPAD` and `TAG_RETAINED_RUN_DIAGNOSTICS`.

AGENTS.md notes that a test image differs from a shipping one "by a `-D` that
leaves no trace in the git hash"; this is that trace.

**Keeping it correct.** The record is a C initializer built from the same
symbols and types the firmware uses: `&sEpoch`, `sizeof(t_StateMarker)`,
`offsetof`, the enum maxima. So it cannot drift from the image, and
`_Static_assert`s catch anything that does not fit its field. The build-time
layout descriptor of item 3 is generated from the same definitions, so the
in-flash record and the package agree by construction. The package descriptor
then carries what is too large for flash: full field tables for every struct,
and the `.proto` files.

**Cost.** A few hundred bytes. Images use 33-59 KB of their 256 KB or 1 MB
(fw-v0.0.3), and the record comes out of the persistent region's capacity, a
few dozen data headers at most.

**How the downloader uses it.**
1. Attach halted.
2. Identify the MCU over SWD (`DBGMCU_IDCODE`).
3. Read the identity record at that MCU's fixed address.
4. Select the loader, and the decoder that turns the capture into the SQLite
   file, by name, confirming with the loader's JEDEC check.
5. Capture.

The processor determines where to look; the record determines everything else.
No board or tag-type argument is needed.

The record changes every image's layout, and on the STM32U375 layout alone has
moved idle current. So it ships only as a qualified release
(`tag_release_check.py`).

### 5. Session facts in the stored configuration

Per-session facts are stored **with the stored configuration (`sconfig`)**. The
stored configuration is erased when the data are cleared and written when the
tag is started, so these facts are rewritten for each session and are never
left describing an earlier one. The record holds:
- **the RV3028 factory EEOffset**: the raw 9-bit steps and the derived ppm, as
  the IMUTag timing design already specifies. `infoAck()` reports the stored
  value instead of reading it live. It is a per-chip constant, read from the
  RV3028 at start and kept beside the configuration it applies to;
- the effective sample period, ODR and ranges, as values rather than enums;
- ideally the nanopb-encoded `Config` itself, which would remove the
  per-family `readConfig()` mapping from the offline path.

This is the session superblock of Field Data Extraction, now given a place. Its
format should be designed together with item 4's record.
