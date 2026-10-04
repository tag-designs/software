---
type: investigation
status: closed
summary: Whether fw-v0.0.3 downloads can be rebuilt from an SWD capture plus the release package, tag by tag, with the gaps and firmware defects the reading found.
---

# Offline Log Reconstruction

Closed. This analysis led to the five decisions listed under
[Decisions and plan](#decisions-and-plan), agreed on 2026-10-01. The rebuild
was built as `tag-rebuild` and validated against live downloads on all five
distributed targets on 2026-10-02/03
([decision 0018](../decisions/0018-offline-rebuild-capture-backed-source.md#consequences)).
How to capture a tag and rebuild its download now is in
[Capturing a Tag](../bench/capturing-a-tag.md).

The analysis answered one
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

Related: [Field Data Extraction](../../embedded/tags/design/proposals/field-data-extraction.md) (why raw dumps are
not self-describing; the session superblock proposal) and
[SWD Capture and Recovery Library](../../host/libraries/tagcore/design/swd-recovery.md)
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
  `families/IMUTag/design/sample-timing.md`.

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
[Tag Firmware: Next Release TODO](../../embedded/tags/TODO.md). An offline rebuild
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

History: the plan agreed on 2026-10-01 is recorded as five decisions:
[1. capture-backed source, with its implementation record](../decisions/0018-offline-rebuild-capture-backed-source.md),
[2. closing the capture gaps](../decisions/0019-offline-rebuild-close-capture-gaps.md),
[3. build-time layout descriptor](../decisions/0017-offline-rebuild-build-time-layout-descriptor.md),
[4. tag identity record](../decisions/0021-offline-rebuild-tag-identity-record.md) and
[5. session facts in the stored configuration](../decisions/0020-offline-rebuild-session-facts-in-stored-config.md).

How the rebuild is kept in step with the firmware is in
[Capturing a Tag](../bench/capturing-a-tag.md#keeping-the-rebuild-in-step-with-the-firmware).
