# UIUCTag Data Collection Integration Plan

## Scope

This note plans **step 1** of UIUCTag bring-up: moving data acquisition and
log storage from the shared BitPresTag implementation to the UIUCTag record
format defined in `include/uiuctag_log_format.h`, and returning that format
through `Ack.uiuctag_data_log`.

It also covers the `host/libraries/tagcore/sqlitelog` decoder, because the
record format is a single contract and the host half is the part that can be
tested without hardware — see [Staged Migration](#staged-migration) for why the
host lands first.

Non-goals:

- sensorViz beyond a display-name entry. UIUCTag reuses the BitPresTag table and
  stream shape, so the viewer discovers its streams from schema metadata with no
  code change.
- Interrupt-driven (`LPS_RDY`) pressure waits. See
  [Deferred work](#deferred-work).
- Any behavior change for `BitPresTag` or `BitPresTagMX25R`.

The messaging layer is already in place: `UIUCTagLog`, `Ack.uiuctag_data_log`,
`UIUCTAG` tag type, the `uiuctag_proto` nanopb target with
`UIUCTagLog skip_message:false` and `Ack.uiuctag_data_log type:FT_STATIC`, and
the shared packed structs in `include/uiuctag_log_format.h`.

## Staged Migration

### Completed: tag-local file structure

| Stage | Work | State |
|---|---|---|
| 1 | Tag-local `state_run.c` | done |
| 2 | Sensor calls extracted to `sensors.[ch]` (W4) | done |
| 3 | Tag-local `datalog.[ch]` | done |

Those stages changed no recorded behavior. The log written today is still the
BitPresTag layout, and a transitional `pressure_sample()` shim in `state_run.c`
converts the float sample from `sensors.c` back to the BitPresTag raw record.

Verification notes carried forward from them:

- `dep/<name>.o.d` in the target's build tree names the file that actually
  compiled; `src/<name>.c` confirms the tag-local override, and the shared
  `core/src/persistent.c` resolves `datalog.h` to the tag-local header **only
  after a clean rebuild**. An incremental build can leave a stale
  `persistent.o` holding the family record type, which matters the moment the
  record size or meaning changes.
- Compare `UIUCTag.list` (disassembly), not the ELF checksum. Every image embeds
  the git hash through the generated `version.h`, so all binaries change
  whenever HEAD moves - including targets the commit did not touch. A clean
  rebuild also varies in other non-code ELF bytes. A checksum is only meaningful
  between two builds at the same commit in the same tree, which is enough to
  show a comment-only or dead-branch-only edit changed no code; anything wider
  needs the disassembly.

### Remaining stages, and why the host goes first

**There is no working UIUCTag download path to protect.**
`isTagLogStorageFormatSupported(UIUCTAG, Sqlite)` is false, so
`defaultTagLogStorageFormat()` falls back to Text, and
`TextTagLogWriter::writeTextLog()` switches on `config_.tag_type()` and reaches
`default: return -1`. `tag-dwnld` reports "Parsing log failed. Unsupported tag
type?" today. The firmware's temporary `Ack.bitprestag_data_log` response
therefore buys nothing, and no compatibility window is needed.

Meanwhile the host decoder is fully testable without hardware: the payload is a
byte image of `t_UIUCTagSample[]` from the shared header, so synthetic blocks
exercise every path. Building the host first makes firmware bring-up a
single-variable exercise — once the tag emits `UIUCTagLog`, any discrepancy is
firmware-side, against a decoder already known good.

| Stage | Work | Depends on | State |
|---|---|---|---|
| S1 | Shared format helpers in `include/uiuctag_log_format.h` | - | done |
| S2 | Host: schema, decoder, dispatch, format registration | S1 | done |
| S3 | Firmware: record, write path, download ack (W2, W3, W5, W6, W7) | S1 | done, hardware run pending |
| S4 | Trim, docs, fixtures, sensorViz display name (W8, W9) | S2, S3 | done except the hardware fixture |

### Where the implementation diverged from this plan

Five decisions were changed while building S3.

1. **Blocks open lazily, and the checkpoint is written after the sample it
   anchors.** The plan opened a block on entry and at each rollover, before
   writing samples. Instead the first sample of a block writes its pressure and
   temperature first, then appends the checkpoint. A reset in between now costs
   one block of samples rather than leaving a checkpoint that points at data
   which was never stored, and every checkpoint describes a block holding at
   least one sample.
2. **No sample at RUNNING entry.** The first sample lands on the first sample
   boundary after entry, so entry does not have to special-case a pressure
   conversion alongside accelerometer setup. At most one slot is lost at run
   start, and it is recorded as an ordinary gap.
3. **Hibernation is permitted at any sample boundary, not only a block
   boundary.** A resumed run anchors a fresh block at its own first sample, so
   there is no reason to defer hibernation by up to two hours. The wake that
   enters hibernation still stores its sample first.
4. **Activity spanning a reset is left unwritten rather than reconstructed.**
   `lastwrite` is cleared on entry, so the sample written before a reset keeps
   an erased activity word. Its accumulation is genuinely lost, and the log says
   so instead of recording a partial count as if it were complete.
5. **The sample grid is anchored at the run's first minute boundary, not at
   absolute epoch multiples.** See the note at the end of
   [Time mapping](#time-mapping-the-invariant-the-host-decoder-depends-on). It
   removed the host's normalization step entirely.

### Verification performed without hardware

Four layers, all reproducible: shared-helper assertions, a host-compiled
simulation of the real `state_run.c` over a synthetic minute-alarm timeline, the
SQLite decoder on synthetic payloads, and an end-to-end pass that decodes the
simulation's own flash images through the real decoder. Together they confirmed
slot addressing across block rollover, one-period-late activity writes with
correct bucket counts, canonical NaN on a failed conversion, checkpoint placement
at window rollover, mid-block reset resumption without double programming,
hibernate/resume block allocation, and that sample epochs, skipped failed
conversions, the hibernation gap, and activity percentages arrive in SQLite where
the sequencer put them.

Rationale, per-layer coverage, and how to run them: [UIUCTag test
strategy](uiuctag-test-strategy.md).

What still needs the tag: real BMP585 and ADXL367 behavior, minute-alarm wake
timing and its energy cost, the capacitor-recharge rest between program cycles,
and the `uiuctag.db3` fixture for `host/docs/fixtures/sensorviz`.

### S1 - Shared format helpers

Additive changes to `include/uiuctag_log_format.h`, which both trees already
include (`MONITORINCDIR` for firmware, `tag_monitor_interface` for host):

- `UIUCTAG_ERASED_WORD` (`0xFFFFFFFFu`) and an activity-present predicate.
- Pressure/temperature presence predicates. Implement them as a **bit-pattern
  NaN test** on a `uint32_t` copy — exponent all ones, mantissa nonzero —
  rather than `isnan()`. That keeps one definition valid in both C and C++ with
  no `math.h`/`cmath` divergence, and unlike `imutag.cc`'s exact-bits test
  against `0x7fc00000` it catches the erased-flash NaN (`0xFFFFFFFF`) as well as
  the canonical one the firmware writes.
- `uiuctagActivityBucket(sample, b)` for the 6-bit unpack.
- `uiuctagSampleEpoch(header_epoch, s)` implementing `epoch + s*300` once, for
  both sides.
- Rename `t_UIUCTagInternalLog.raw_voltage` to state its 0.01 V units.

*Acceptance:* both trees build; existing size assertions still hold; no
behavior change anywhere.

### S2 - Host decoder

Follows the recipe in the `sqlitelog.cc` header comment and the raw-binary model
in `sqlitelog/imutag.cc`:

| File | Change |
|---|---|
| `taglogwriter.cc` | add `UIUCTAG` to the Sqlite-supported list, which also makes `.db3` its default output |
| `sqlitelog/schema.cc` | `case UIUCTAG:` returning `{voltageTable(), activityTable(), pressureTable(), sensorTemperatureTable()}` - identical to `BITPRESTAG` |
| `sqlitelog/internal.h` | declare `dumpUIUCTagLog()` |
| `sqlitelog/uiuctag.cc` (new) | the decoder; add to `libraries/tagcore/CMakeLists.txt` |
| `sqlitelog.cc` | `case UIUCTAG:` dispatch, and the supported-tag list in its class comment |
| `txtlogs.cc` | minimal `dumpTagLog(std::ostream &, const UIUCTagLog &)` and case, so `--format text` does not hard-fail |
| `sqlitelog/README.md` | a "UIUCTag Downloader Fields" section, matching the IMUTag one |

Decoder contract:

- Validate `size % UIUCTAG_SAMPLE_SIZE == 0` and
  `size <= UIUCTAG_SAMPLE_BYTES_MAX`; a short payload is a valid partial block,
  unlike the IMUTag decoder which requires an exact page.
- `memcpy` each sample out of the payload rather than casting: protobuf `bytes`
  storage carries no alignment guarantee and `t_UIUCTagSample` is packed with
  floats.
- Take all geometry from the shared macros, never literals. Note the existing
  divergence this avoids: `sqlitelog/pressure.cc` decodes BitPresTag activity as
  4 buckets of 4 bits over 15 s while `txtlogs.cc` decodes the same message as
  5 buckets of 6 bits over 60 s. One of those is wrong; it is a pre-existing
  BitPresTag bug, tracked separately, not fixed here.
- Rows, with `block_start = epoch - epoch % UIUCTAG_DATA_LOG_SECONDS`:
  - `Voltage`: one row per ACK at the raw `epoch`, since the reading is taken as
    the block opens.
  - `Pressure`, `SensorTemperature`: one row per sample at
    `block_start + s*300`, **skipped** when the value is NaN, so gaps stay gaps
    instead of becoming zeros.
  - `Activity`: one row per bucket at `block_start + s*300 + b*60`, value
    `count * 100.0 / 60` percent; the whole sample is skipped when the activity
    word is erased.
- Return 1 per ACK, matching the `TagLogWriter` records-consumed convention.

*Acceptance:* a throwaway generator feeds synthetic `UIUCTagLog` blocks through
`SqliteTagLogWriter` - full block, partial block, all-erased block, interior NaN
sample, activity ramp covering 0 and 60 - and `sqlite3` queries confirm row
counts, timestamps, and that gaps produce no rows. This ships inert: nothing
emits `UIUCTagLog` until S3.

### S3 - Firmware record, writes, and download

Three sub-steps, each buildable, with two hardware checkpoints:

- **S3a** - `datalog.h` record types (`t_DataHeader` becomes the 8-byte
  checkpoint and retypes `vddHeader[]`), the chunked
  `writeSampleWord()`/begin/end write API (W3), `writeDataHeader()` adapted, and
  the new `restoreLog()` (W6). **Requires a clean rebuild**, per the stale
  `persistent.o` note above.
- **S3b** - `data_logAck()` emits `Ack_uiuctag_data_log_tag` with the raw block
  (W7). *Checkpoint 1:* download an erased or partially written log. Framing,
  index bounds, `NODATA` past `pages`, and trailing-sample trimming are all
  observable here, through the S2 decoder, before any real data exists.
- **S3c** - `state_run.c` sequencing (W5): minute alarm, run-anchored grid,
  staged writes with recharge rests, NaN on sensor failure, checkpoint on block
  open, activity flush before `Hibernating()`/`Finished()`. Deletes the
  transitional `pressure_sample()` shim. *Checkpoint 2:* the full capture
  sequence in [Verification](#w10--verification).

**Note on the checkpoint record.** It changed meaning, from
`{epoch, vdd100[2]}` to `{epoch, vdd100, extern_log_block}`, at the same 8-byte
size, and nothing detects the difference: stale headers would simply be counted
by `countInternalBlocks()` and read as UIUCTag checkpoints. No deployed tag
predates this work, so there is nothing to migrate — erase a bring-up board once
if it ran the intermediate BitPresTag-format firmware, and keep the same caution
in mind for any future change to this record.

### S4 - Trim and finish

- Drop `BitPresTagLog skip_message:false` and
  `Ack.bitprestag_data_log type:FT_STATIC` from the UIUCTag nanopb overrides
  (W8), and confirm the `PROTOBUFSIZE` headroom that frees.
- `displayNameForTag()` in `sensorviz/sqlite_loader.cpp`: add "UIUCTag". Without
  it the viewer falls through to showing the raw `UIUCTAG` string, which is
  cosmetic only.
- Documentation (W9), plus a `uiuctag.db3` fixture in
  `host/docs/fixtures/sensorviz` from a real download, alongside the existing
  `imutag.db3` and `compasstag.db3`.

Rollback: S1 is additive, S2 is inert without firmware, and S3 is revertable per
sub-step - subject to the erase note above, since flash contents do not roll
back with the firmware.

## Constraints Verified in the Tree

These are the facts the plan relies on; they were checked against the current
worktree rather than assumed.

1. **Variant source override already works.** ChibiOS `rules.mk` derives object
   names with `$(notdir ...)` and resolves the `%.c` prerequisite through
   `VPATH`, and `embedded/tags/common/make.mk` puts `./src` ahead of
   `$(TAG_FAMILY_SRC_DIRS)`. A UIUCTag-local `src/<name>.c` therefore replaces
   the family file of the same name even though `family.mk` lists the family
   file by full path. This is already load-bearing: `UIUCTag.elf` links
   `uiucTagAccelDevice` from `embedded/tags/UIUCTag/src/devices.c`, not the
   family `devices.c` (which would not compile against this board's
   `LINE_ACCEL_nCS`/`LINE_LPS_nCS` names). `./inc` likewise precedes
   `$(TAG_FAMILY_INC_DIRS)` in `INCDIR`.
   *Consequence: the entire step can be done in UIUCTag-local files, with zero
   edits to family sources.*
2. **Common code touches the log through four entry points only:**
   `restoreLog()`, `eraseExternal*()`, `data_logAck(index, ack)`, and the
   monitor status fields `internal_data_count = pState->pages` /
   `external_data_count = pState->external_blocks`
   (`embedded/tags/common/core/src/monitor.c`,
   `embedded/tags/common/core/src/state_machine.c`).
3. **`vddHeader` is declared in common code but typed by the tag.**
   `embedded/tags/common/core/src/persistent.c` declares
   `t_DataHeader vddHeader[256]` in `.persistent`; `t_DataHeader` comes from the
   tag's `datalog.h`. A UIUCTag-local `datalog.h` that defines `t_DataHeader` as
   the 8-byte UIUCTag checkpoint keeps that declaration compiling unchanged and
   keeps the declared extent at one 2048-byte flash page (8 B x 256), which is
   why that bound was chosen.
   **The `[256]` is not the capacity.** `vddHeader` runs to the end of the
   `.persistent` region, and both `readDataHeader()` and `writeDataHeader()`
   bound against `__persistent_end__` rather than the array extent. Firmware
   must keep doing that; nothing may treat 256 as the checkpoint limit.
4. **The shared log header is on the firmware include path.**
   `MONITORINCDIR` is the top-level `include/` directory
   (`tag_monitor_interface`) and is in `UINCDIR`, so firmware can
   `#include "uiuctag_log_format.h"` the way IMUTag includes
   `imutag_log_format.h`.
5. **The RTC ticker is free-running; the minute alarm is epoch-aligned.**
   `enableTicker()` programs the periodic wakeup timer from `ck_spre`, so
   wakeups land every N seconds from whenever RUNNING started and
   `timestamp % 300 == 0` does **not** hold. `enableAlarm(0, ALARM_MINUTE)`
   (`embedded/tags/common/core/src/time.c`) instead masks the alarm down to a
   seconds-field match, so it fires on every epoch minute boundary and posts
   `EVT_RTC_ALRAF`. UIUCTag uses the minute alarm so that the sample grid,
   anchored at the run's first minute boundary, falls on whole minutes and each
   sample covers exactly five of them. Precedent:
   `BitTagNG/src/state_run.c` and `BitTag/src/bt_state_run.c` both drive
   RUNNING from alarm 0 this way; `state_machine.c` uses alarm 1 for the
   non-RUNNING states, so alarm 0 is free.
6. **The pressure driver has all required primitives.**
   `bmp581_config_forced_device()`, `bmp581_trigger_forced_device()`,
   `bmp581_read_int_status...()`, `bmp581_read_pressure_temp_powered_device()`,
   and the polling convenience wrapper
   `bmp581_sample_forced_blocking_device()`. Pressure is returned as
   `float` hPa; temperature as `int16_t` centi-degrees C.
7. **External flash writes may be arbitrary offset and length.**
   `at25xeWrite()` splits at 256-byte page boundaries, issues WREN per page
   program, and polls WIP, so 4-byte chunk writes at any address are safe.
   The 288-byte block stride is not a multiple of 256, so an occasional 4-byte
   chunk straddles a page boundary and costs two program cycles.
8. **Capacity is not a constraint.** `vddHeader` starts at 0x0800AA60 and the
   `.persistent` region ends at flash end, 0x08040000: 0x355A0 = 218 528 bytes,
   or ~27 300 eight-byte checkpoints (`writeDataHeader()` stops ~16 bytes
   short). At one checkpoint per 2 h that is over six years. External flash is
   4 MB (`AT25XE_SIZE`) = 14 563 blocks of 288 bytes, ~3.3 years, so external
   storage is the binding limit and neither is a practical concern.
   One consequence: `countInternalBlocks()` is an O(n) linear scan over
   checkpoints. It runs only from `restoreLog()` on the power-on / brownout /
   exception recovery path — a normal standby wake keeps `pState->pages` in
   backup registers — so its cost stays off the per-wake energy budget. Worth
   revisiting only if that recovery path becomes hot.
9. **Backup state has room.** `BackupState` uses 13 words of the 32 available
   RTC backup registers. The design below needs no new fields, but adding one
   or two would be safe.
10. **The float-sentinel convention already exists.**
    `families/IMUTag/src/sensors.c` writes `__builtin_nanf("")` through a
    `missing_aux_sample()` helper whenever an auxiliary sample is not available,
    precisely so host loaders can tell "not sampled" from a real zero or a held
    value. UIUCTag stores NaN in `pressure`/`temperature` the same way.

## Record Layout and Timing Contract

Geometry comes from `include/uiuctag_log_format.h` and is **not** redefined in
firmware:

| Quantity | Value | Source |
|---|---|---|
| Activity bucket | 60 s, 6 bits | `UIUCTAG_ACTIVITY_BUCKET_SECONDS`, `UIUCTAG_ACTIVITY_BUCKET_MASK` |
| Buckets per sample | 5 | `UIUCTAG_ACTIVITY_BUCKETS_PER_EXTERNAL_BLOCK` |
| Sample period | 300 s | `UIUCTAG_EXTERNAL_BLOCK_SECONDS` |
| Samples per block | 24 | `UIUCTAG_LOG_SAMPLES` |
| Block period | 7200 s (2 h) | `UIUCTAG_DATA_LOG_SECONDS` |
| Sample record | 12 B | `t_UIUCTagSample` |
| External block stride | 288 B | `UIUCTAG_SAMPLE_BYTES_MAX` |
| Internal checkpoint | 8 B | `t_UIUCTagInternalLog` |

**Note on the existing board-integration note:** `uiuctag-board-integration.md`
says "one data-log block covers six five-minute samples, or 30 minutes" and
"30-minute internal checkpoints", which contradicts `samples[24]` in the same
document, `UIUCTAG_LOG_SAMPLES 24`, and `UIUCTagLog.samples max_size:288`. This
plan follows the header and the protobuf bound: **24 samples, 2 hours per
block**. The 30-minute wording is stale and is corrected as part of W9.

This rests on the format artifacts alone, not on storage capacity: per
constraint 8, either block size stores years of data. The real tradeoff is
download granularity and checkpoint density — 24 samples means 2 h of data per
download round-trip and one internal write per 2 h; 6 samples means 30 min per
round-trip, four times the checkpoints, and four times the download
transactions for the same deployment. Changing it means changing
`UIUCTAG_LOG_SAMPLES`, `UIUCTagLog.samples max_size`, and the
`t_UIUCTagDataLog` size assertion together, so it is cheapest to settle before
W2.

### Time mapping (the invariant the host decoder depends on)

RUNNING wakes on the minute alarm. The **first** such wake of a run writes the
first checkpoint and the first sample, and that instant anchors the sample grid
for everything after it:

```
first sample          = first minute boundary at or after RUNNING entry
sample s of a block   = checkpoint.epoch + s * 300
next block starts at  = checkpoint.epoch + 24 * 300
bucket b of sample s  = sample epoch + b * 60
```

For checkpoint `k` with fields `{epoch, vdd100, extern_log_block}`:

- Block bytes live at `extern_log_block * 288` in external flash.
- `epoch` is the time of this block's **own slot 0**, not a rounded boundary.
  The host adds `s * 300` and is done: no modulo, no absolute grid, and no way
  for two blocks to share a start time.
- `samples[s].pressure` / `.temperature` are instantaneous readings taken at
  that slot time.
- `packed_activity_data` bucket `b` counts active seconds in the `b`-th minute
  after the slot time; range 0..60, so the 6-bit field never saturates.
- **Every checkpoint maps to exactly one 288-byte external block, without
  exception.** A restart may skip individual fields or whole slots, but never
  breaks the one-header-to-one-block correspondence, so block index equals
  checkpoint index and the host needs no fixups.
- Missing data is always a NaN float. Erased flash reads as `0xFFFFFFFF`, which
  *is* a quiet NaN, and a failed sensor read stores the canonical
  `__builtin_nanf("")`. One host rule covers both: **NaN means no
  measurement.** For activity, `packed_activity_data == 0xFFFFFFFF` means the
  word was never written. Gaps may appear anywhere in a block, not just at the
  tail.

An earlier revision of this plan anchored the grid to absolute epoch multiples
of 7200 instead, and had the host round a header epoch down to its window. That
worked, but it cost more than it bought: the first block of a run wasted the
slots before the run started, a header epoch and its slot 0 were different
instants, and two blocks could share a normalized start after a restart inside
one window. Anchoring at the run's first minute boundary removes all three, and
reduces the host's time reconstruction to one addition.

### Write staging

Each RTC wake performs at most three 4-byte programs, each followed by a rest
so the storage capacitor recharges:

1. `packed_activity_data` of the **previous** sample, at `g_prev*12 + 8`.
2. `pressure` of the current sample, at `g*12 + 0`.
3. `temperature` of the current sample, at `g*12 + 4`.

where `g` is the global sample index. Flash is woken once before step 1 and
slept once after step 3.

### Cursor derivation (no new persistent state)

`pState->pages` counts checkpoints, so the current block index is `pages - 1`,
and the newest checkpoint is read back from internal flash (memory-mapped, no
external flash access). At a minute-alarm wake at time `T`:

```
C    = checkpoint[pages-1]                       // absent on the first wake
due  = (T - C.epoch) % 300 == 0                  // is a sample due?
slot = (T - C.epoch) / 300
full = slot >= 24                                // block finished
g    = C.extern_log_block * 24 + slot            // global sample index
```

The grid lives in flash, not in absolute time, which is what makes recovery
exact rather than approximate: a reset mid-block leaves `C` untouched, so the
resumed run lands on the very sample times it would have used had it never
stopped. A minute wake that is not on the grid simply is not a sample boundary —
worth noting, because after a reset the tag will wake up to four times before
its next sample.

When `full`, a new block is opened with `epoch = T` and
`extern_log_block = C.extern_log_block + 1`, and its slot 0 is written in the
same wake. A long gap — hibernation, or a clock that was stopped — lands far past
the last slot and re-anchors the grid at that wake rather than leaving the
skipped slots addressable.

`pState->lastwrite` holds the epoch of the last sample write. It is not a cursor:
it is the start of the activity window currently accumulating, and it suppresses
the pending-activity write when the previous sample was not in the immediately
preceding window (first sample of a run, hibernation resume, or a reset).

Activity accumulation is measured from that window start: each active second is
credited to bucket `(i - lastwrite) / 60`, and seconds falling outside the five
buckets are dropped rather than wrapped, so a late wake cannot corrupt the
current sample's counts.

## Work Items

### W1 — Split UIUCTag off the family log/state code (no behavior change)

Add UIUCTag-local copies that are byte-for-byte behavior-equivalent to today's
family files, and confirm the override:

- `embedded/tags/UIUCTag/inc/datalog.h`
- `embedded/tags/UIUCTag/src/datalog.c`
- `embedded/tags/UIUCTag/src/state_run.c`

`sensors.[ch]` (W4) is new rather than a copy, so it is added to
`embedded/tags/UIUCTag/project.mk` via `ALLCSRC += sensors.c` — the same way
`IMUTagNandBmp581` adds `power_modes.c`. Nothing in the family manifest changes.

Optionally (recommended for legibility, not required for correctness) change
`embedded/tags/families/BitPresTag/family.mk` to list bare filenames the way
`families/IMUTag/family.mk` does, so the override is explicit rather than an
emergent `VPATH` property. This is a no-op for the other two variants, which
resolve the same family files.

*Acceptance:* `UIUCTag`, `BitPresTag`, and `BitPresTagMX25R` all build warning
free; `dep/state_run.o.d` and `dep/datalog.o.d` name `src/...` paths; the two
sibling ELFs are unchanged.

### W2 — UIUCTag log types and geometry

In the local `datalog.h`:

- `#include "uiuctag_log_format.h"`.
- `typedef t_UIUCTagInternalLog t_DataHeader;` (or a struct with identical
  layout), keeping `sizeof == 8` so common `persistent.c` and the double-word
  `FLASH_Program_Array()` path in `writeDataHeader()` are unaffected. Add a
  static assertion.
- Replace `DATALOG_SAMPLES` / `t_DataLog` with the header's names; drop the
  BitPresTag `t_DataLog` entirely (nothing local should keep a 288-byte RAM
  buffer — the download path writes straight into the ack payload, as
  `families/IMUTag/src/datalog.c` does).
- Declare the new log IO API (W3).

Add the shared decode helpers to `include/uiuctag_log_format.h` (stage S1) so
the host decoder gets them for free rather than reimplementing the convention:

- `UIUCTAG_ERASED_WORD` (`0xFFFFFFFFu`) and a `uiuctagSampleHasActivity()`
  predicate.
- `uiuctagSampleHasPressure()` / `...HasTemperature()`, defined as
  `!isnan(...)`, which covers erased flash and an explicit sensor-failure NaN
  with one test (constraint 10).
- `uiuctagActivityBucket(sample, b)` for the 6-bit unpack, and
  `uiuctagSampleEpoch(header_epoch, s)` implementing `epoch + s*300` once, on
  both sides of the link.

Also rename `t_UIUCTagInternalLog.raw_voltage` to make its units explicit (it
carries `vdd100`, i.e. 0.01 V units, matching `Status.voltage` handling
elsewhere) — nothing consumes the field yet, so this is free to fix now.

### W3 — Offset-addressed, energy-paced external write API

Replace the family `writeDataLog(uint16_t *data, int num)` (append-only,
whole-record, sample-count cursor) with, in the UIUCTag local `datalog.c`:

```c
enum LOGERR writeSampleWord(uint32_t sample_index, uint32_t field_offset,
                            const void *word);
```

- Address = `sample_index * UIUCTAG_SAMPLE_SIZE + field_offset`.
- Bounds-checked against `externalFlashSize()`; returns `LOGWRITE_FULL`
  otherwise.
- Programs exactly 4 bytes, then rests `UIUCTAG_WRITE_REST_MS` (default 20 ms,
  overridable from `custom.h`) using `stopMilliseconds()` so the MCU sleeps
  through the recharge interval.
- Flash wake/sleep is **not** inside this call; a `writeSampleBegin()` /
  `writeSampleEnd()` pair wraps the wake's up-to-three programs so the chip
  leaves deep power-down once per wake.

Keep `writeDataHeader()`, `restoreLog()`, `eraseExternal*()`,
`externalFlashSize()`, and the `fast_msi()`/`slow_msi()` helpers, adapted to
the new header type.

*Acceptance:* a bring-up test writes a full block through the API and reads
back the expected 288 bytes; scope/current probe confirms the rest interval
lands between program cycles.

### W4 — Extract a UIUCTag `sensors.c` / `sensors.h`

Follow the `families/IMUTag` split: `state_run.c` owns time, state, and log
sequencing, and knows nothing about ADXL367 or BMP58x registers. New files
`embedded/tags/UIUCTag/{inc/sensors.h,src/sensors.c}` own:

- `bool initDataCollection(void)` — the current `accel_init()` body (ADXL367
  branch only; the local file no longer needs the
  `TAG_SENSOR_ACCEL_ADXL367`/`ADXL362` fork) plus any pressure-side
  configuration.
- `bool samplePressure(float *pressure_hpa, float *temperature_c)` — wraps
  `bmp581_config_forced_device()` +
  `bmp581_sample_forced_blocking_device()`, converts the driver's `int16_t`
  centi-degrees to float degrees C, powers the sensor down, and on any failure
  fills **both** outputs with `missing_sample()` returning
  `__builtin_nanf("")`, mirroring IMUTag's `missing_aux_sample()`
  (constraint 10). Returns false as well, so the caller can count failures, but
  the outputs are always safe to store.
- `bool deinitDataCollection(void)` — the quiescing path used on
  FINISHED/HIBERNATING entry.

This is what makes the eventual `LPS_RDY` change (deferred) a
`sensors.c`-local edit rather than a state-machine change.

### W5 — RUNNING state sequencing

Rewrite the UIUCTag `state_run.c` `Running()`:

- Constants from the shared header, with `UIUCTAG_SAMPLE_PERIOD_SEC`
  overridable in `custom.h` for bring-up (default 300). Delete the family's
  debug `60/15/4/4` constants — do not inherit them.
- **Wake source: the minute alarm, not the ticker.** `T_INIT` does
  `disableAllAlarms(); disableTicker(); enableAlarm(0, ALARM_MINUTE);` in that
  order, and the sample work is driven by `events & EVT_RTC_ALRAF`, matching
  `BitTagNG/src/state_run.c`. Re-arm the alarm on
  `reason == State_EVENT_EXCEPTION` as BitTagNG does.
- Activity accumulation runs at every wake (minute alarm and `EVT_WKUP`
  activity edges alike), with bucket indexing measured from the start of the
  sample window being accumulated: `index = ((i - lastwrite) / 60) * 6`. Drop
  seconds falling outside the five buckets rather than wrapping them, and clamp
  each bucket to `UIUCTAG_ACTIVITY_BUCKET_MASK`.
- Sample work happens only on the 300-second boundaries (`T % 300 == 0`), so
  four of every five minute wakes touch neither the pressure sensor nor flash.
- `T_INIT`: sample ADC, reset activity state, `initDataCollection()`, arm the
  alarm, write the first checkpoint, and — if `T % 300 == 0` — take the slot-`s`
  sample immediately; otherwise the first sample lands on the next boundary and
  the earlier slots of that block stay erased.
- On a sample boundary: flush the previous sample's activity word (unless
  suppressed per the cursor rules); open a new block if the block epoch changed;
  `samplePressure()`; write pressure and temperature; update `lastwrite`,
  `activity`, `lastactstart`.
- Sensor failure stores NaN in both fields rather than skipping the write, so
  "the tag was awake and tried" is distinguishable from "no wake happened"
  (erased). Both decode as missing on the host.
- Voltage: keep the family's `vdd100` running average; store it in the
  checkpoint when a block opens.
- Flush the pending activity word before returning `Hibernating(...)` or
  `Finished(...)`, otherwise the last sample of every run loses its activity.
  Hibernation is still permitted only on a block boundary; resume opens a fresh
  block with a fresh checkpoint.
- `LOGWRITE_FULL` from either the checkpoint region or the external cursor ends
  the run through `Finished(T_INIT, State_EVENT_INTERNALFULL)` as today.

*Energy note:* the minute alarm means five standby wakes per sample instead of
one. Four are short — RTC read, activity accounting, back to standby — and
BitTag/BitTagNG already run this cadence on comparable hardware, so it is a
proven cost. If it measures badly, the fallback is reprogramming an `mm:ss`
alarm to the next 5-minute boundary each wake, which keeps epoch alignment at
one wake per sample but needs a new helper in common `time.c`.

### W6 — Cursor recovery

`restoreLog()` becomes:

```
pState->pages = countInternalBlocks();
pState->external_blocks = pages;   // download units, see W7
```

No mid-block scan is needed: W5 derives the slot address from the flash
checkpoint and `timestamp`, so a reset mid-block resumes at the correct slot
and never reprograms a written one. (This is the main reason to prefer the
time-derived cursor over a RAM sample counter.) Note the family version's
`pages * DATALOG_SAMPLES * 2` — which disagrees with `Running()`'s
`pages * DATALOG_SAMPLES` — is not carried over.

### W7 — Download path

Rewrite `data_logAck(index, ack)` in the UIUCTag `datalog.c`:

- `index` is a **checkpoint index**, one per 24-sample block.
- Validate against `pState->pages` and the persistent-region end; invalid or
  unwritten (`epoch == -1`) → `ack->err = Ack_Err_NODATA`,
  `which_payload = 0`, matching the IMUTag pattern the host downloader already
  tolerates.
- `which_payload = Ack_uiuctag_data_log_tag`; `epoch = C.epoch`;
  `voltage = C.vdd100 * 0.01f`.
- Read `288` bytes from `C.extern_log_block * 288` directly into
  `log->samples.bytes`, then set `log->samples.size` by trimming only the
  *trailing* fully-erased samples, so a partial final block returns just what it
  holds. Interior gaps stay in the payload — they carry position, and position
  carries time — and are read as NaN by the host.
- No unit conversion: `pressure` is already hPa and `temperature` already
  degrees C in the stored record, so the family's
  `bitprestagLogPressure()`/`...Temperature()` helpers disappear. The
  centi-degree to float conversion happens once, in `sensors.c` at sample time.
- Keep the `fast_msi()`/`HIGHPRIO` wrapper.

Then set `pState->external_blocks = pState->pages` (blocks, not samples) in
`Running()` and `eraseExternalFinish()`, so `external_data_count` is a valid
upper bound for the existing host download loop.

### W8 — Trim the temporary protobuf surface

Once W7 lands, drop `BitPresTagLog skip_message:false` from
`embedded/proto-c/uiuctag-proto-c/tagdata.override.options` and
`Ack.bitprestag_data_log type:FT_STATIC` from `tag.override.options`. Confirm
`Config.adxl362`/`Adxl362` stay enabled (the ADXL367 reuses those fields; the
family README already documents `inactive_sec` as a sample count for this
family). No `Config` or `t_storedconfig` change is expected — the sample period
is firmware-fixed, and `default-config.json` needs no edit.

### W9 — Documentation

- Rewrite the "Firmware Integration State" and "Log Schema" sections of
  `uiuctag-board-integration.md`: fix the 30-minute/6-sample wording, and point
  the remaining-work list at this note.
- Note in `families/BitPresTag/README.md` that UIUCTag owns its own
  `datalog.[ch]`/`state_run.c` and why (different record format), so the next
  reader does not "unify" them.
- Register this note in `docs/developer/src/source-tree.md`,
  `docs/developer/mkdocs.yml`, and `DEVELOPER_DOCS_MARKDOWN` in the top-level
  `CMakeLists.txt`, per AGENTS.md.
- `embedded/tags/BUILD_SOURCES.md` if the family/local source split changes what
  is listed there. Note it currently has no UIUCTag section at all and still
  predates the TagUIUC rename, so it is stale independently of this work.
- `host/libraries/tagcore/sqlitelog/README.md`: a "UIUCTag Downloader Fields"
  section describing the tables, row timing, and the NaN-means-missing rule,
  matching the existing IMUTag section.
- The supported-tag list in the `SqliteTagLogWriter` class comment in
  `sqlitelog.h`.

### W10 — Verification

Desk checks:

0. Host: the synthetic-payload run from stage S2 - full, partial, all-erased,
   interior-NaN, and 0/60 activity-edge blocks - reproduces the expected rows,
   and `tag-dwnld` selects `.db3` by default for a UIUCTAG config.
1. All three BitPresTag-family targets build; the other two are bit-identical.
2. Static assertions on `sizeof(t_DataHeader) == 8` and the shared-header sizes.
3. `UIUCTag.map`: `.persistent` layout unchanged (2 KB `vddHeader`), and no new
   288-byte RAM buffer.

On hardware:

4. `RUN_ADXL362` (ADXL367 case) and `RUN_LPS` (BMP585 chip ID) still pass.
5. Minute-alarm cadence: confirm wakes land on `seconds == 0` and that sample
   writes land only on epoch multiples of 300, with four of five minute wakes
   touching neither sensor nor flash.
6. Short-period run (`UIUCTAG_SAMPLE_PERIOD_SEC` reduced) through at least
   three block rollovers; download every block and check the reconstructed
   pressure/temperature series and per-minute activity buckets against a
   shaken/still stimulus. Verify bucket order — bucket 0 first — by moving the
   tag during one known minute of a 5-minute window.
7. Pull the pressure sensor's bus or force a driver error, and confirm the slot
   stores a canonical NaN while the surrounding slots keep real values.
8. Reset mid-block (unplug/replug), then confirm the run resumes at the correct
   slot and no slot is double-programmed (read-back equals the union of both
   segments, with the reset gap erased), and that the new checkpoint still maps
   one-to-one onto a fresh block.
9. Hibernation window across a block boundary: the last pre-hibernation sample
   keeps its activity word; the post-resume block starts a new checkpoint.
10. Full-flash and full-checkpoint termination reach FINISHED cleanly.
11. Current-probe one sample wake: three separated program pulses with the rest
    interval between them, the MCU in stop mode during the rests, and a
    measurably cheaper non-sample minute wake.

## Firmware/Host Interface Contract

What the firmware guarantees to the `sqlitelog` decoder:

- One `Ack.uiuctag_data_log` per **2-hour block**; download index space is
  `0 .. internal_data_count-1`, and `external_data_count` equals that count.
  One checkpoint maps to one block without exception, so there are no holes in
  the index space.
- `voltage` is volts; `samples` is `n * 12` bytes of `t_UIUCTagSample`,
  `n <= 24`, always starting at slot 0 so the array index *is* the slot number.
- `epoch` is the time of the block's own slot 0. Sample `s` is at
  `epoch + s*300` and its activity bucket `b` covers `epoch + s*300 + b*60`,
  value 0..60 active seconds. Do not round the epoch to a 2-hour boundary: the
  grid is anchored at the run's first minute boundary, so a header epoch is
  generally not a multiple of the block period, and rounding would shift every
  sample in the block.
- **NaN means no measurement**, for both erased slots (`0xFFFFFFFF`, itself a
  NaN) and failed sensor reads (canonical quiet NaN). Activity is absent when
  `packed_activity_data == 0xFFFFFFFF`. Gaps may appear anywhere in the payload,
  so the host must skip them rather than store them as zeros — a NaN or a zero
  would otherwise poison plots and SQLite aggregates alike.

## Deferred Work

- **`LPS_RDY` interrupt-driven pressure wait.** W4/W5 keep
  `bmp581_sample_forced_blocking_device()` polling, which is already proven in
  the family path. Sleeping on the DRDY EXTI line saves only the conversion
  window (tens of ms per 5 min) and requires wake-source plumbing in
  `pwr.c`/`devices.c`, so it is better done as a separate, measurable change
  after collection is correct. W4 is what keeps that change local: it lands
  inside `sensors.c` behind the same `samplePressure()` contract.
- **Internal-temperature logging.** The UIUCTag record keeps only voltage in
  the checkpoint; the family's `temp10`-in-`vdd100[1]` slot has no equivalent.
  If MCU temperature is wanted, it needs a format change, not a firmware-only
  one.

## Related Notes

- [UIUCTag board integration plan](uiuctag-board-integration.md)
- [BMP581/BMP585 forced-mode pressure plan](bmp581-forced-mode.md)
