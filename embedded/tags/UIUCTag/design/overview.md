---
type: design
status: current
summary: What UIUCTag is, how its hardware differs from BitPresTag, and the minute-record layout, timing and firmware/host contract of its pressure and activity log.
---

# UIUCTag Design

UIUCTag is a BitPresTag-family tag that logs **one 8-byte record every
minute**: a raw BMP585 pressure and temperature reading, and three counts of
active seconds, one per 20 s, for the minute before the reading. Thirty-two
records fill one 256-byte block, exactly one AT25XE page, anchored by one
internal-flash checkpoint. Record times are not stored: a block's checkpoint
holds the epoch of its record 0, and record `s` is at `epoch + s * 60`. A
record that was never written reads erased; a failed reading stores zero; a
minute the tag did not watch stores the reserved bucket value `0x1F`. The
4 MiB external flash holds **364.1 days**. UIUCTag reuses the family state
machine, tests, storage modules and ChibiOS configuration, and replaces the
family's sensors, log format and RUNNING state with its own. Why this format:
[decision 0044](../../../../docs/decisions/0044-uiuctag-minute-records.md).

## Hardware

| Component | BitPresTag baseline | UIUCTag |
| --- | --- | --- |
| Accelerometer | ADXL362 on SPI2 | ADXL367 on USART2, synchronous 4-wire SPI (CPOL 0, CPHA 0) |
| Pressure sensor | LPS27 on USART1 synchronous SPI | BMP585 on SPI1 |
| Pressure interrupt | none | `LPS_RDY` line, wired but unused by firmware |
| External flash | AT25XE or MX25R | AT25XE321D, 4 MB |

- **ADXL367.** Bound through the shared `TagBusDevice` transport with
  `TAG_BUS_USART_INIT()` on the generated `LINE_ACCEL_*` lines; the driver is
  the same whether the bus is SPI or USART. It runs in wake mode, nominally
  6.25 Hz, for activity detection; bring-up measured the inactivity timing
  faster than that nominal (roughly 100-130 ms per sample rather than 160 ms),
  which is still open
  ([bring-up report](investigations/bringup-report.md#adxl367-wake-mode-sample-rate-timing)).
- **BMP585.** Software-compatible with the BMP581: the shared
  `common/sensors/pressure/src/bmp581.c` accepts both chip IDs (`0x50`,
  `0x51`). UIUCTag power-cycles the sensor rail for every reading, uses
  `bmp581_config_forced_fast_device()`, which writes only the registers a
  power-on-reset part needs, and reads the six data registers unconverted with
  `bmp581_sample_forced_raw_blocking_device()`. The conversion wait **polls** `INT_STATUS` with
  `stopMilliseconds()` sleeps; an interrupt-driven wait on `LPS_RDY` was
  evaluated and deferred, because no proven hardware timeout bounds a WFI wait
  ([bring-up report](investigations/bringup-report.md), 2026-09-27).

## Source ownership

UIUCTag-local `src/state_run.c`, `src/datalog.c`, `inc/datalog.h`,
`src/config.c`, `src/devices.c` and `inc/devices.h` replace the BitPresTag
family files of the same name (a tag's `./src` and `./inc` are searched first).
`src/sensors.c` and `inc/sensors.h` have no family counterpart; `project.mk`
adds `sensors.c` to `ALLCSRC` itself. `inc/custom.h` is tag-local, as in every
variant. The record formats genuinely differ, so these should not be merged
back into the family. `state_run.c` owns time, state and log sequencing and knows nothing
about sensor registers; `sensors.c` owns the ADXL367 and BMP585.

The shared format, `include/uiuctag_log_format.h`, is on both the firmware and
host include paths and is the single source of geometry and decode helpers.

## Record layout

The shared header `include/uiuctag_log_format.h` is the definition; this is a
summary.

| Bytes | Field | Contents |
| --- | --- | --- |
| 0-1 | activity | three 5-bit counts, 0-20 active seconds per 20 s; bucket `b` in bits `5b..5b+4`; bit 15 always 0 |
| 2-4 | pressure | BMP585 `PRESS_DATA`, unsigned 24-bit LE; hPa = raw / 6400 |
| 5-7 | temperature | BMP585 `TEMP_DATA`, signed 24-bit LE; degC = raw / 65536 |

The data registers already hold compensated values: "raw" means unconverted.
The tag spends no energy on units; the host converts.

| Quantity | Value | Source |
| --- | --- | --- |
| Record | 8 B, one per minute | `t_UIUCTagRecord`, `UIUCTAG_RECORD_SECONDS` |
| Activity bucket | 20 s, 5 bits, `0x1F` reserved | `UIUCTAG_ACTIVITY_BUCKET_SECONDS`, `UIUCTAG_ACTIVITY_NOT_OBSERVED` |
| Records per block | 32 | `UIUCTAG_LOG_RECORDS` |
| Block | 256 B (one AT25XE page), 1920 s | `UIUCTAG_BLOCK_BYTES`, `UIUCTAG_DATA_LOG_SECONDS` |
| Internal checkpoint | 8 B: `int32 epoch`, `uint16 vdd100` (0.01 V), `uint16 extern_log_block` | `t_UIUCTagInternalLog` |

The checkpoint is the tag's `t_DataHeader`, so the common
`core/src/persistent.c` declaration `t_DataHeader vddHeader[256]` stores it.
**The `[256]` is not the capacity**: `readDataHeader()` and `writeDataHeader()`
bound against `__persistent_end__`.

### Missing and reserved values

| Value | Meaning | Host |
| --- | --- | --- |
| activity word `0xFFFF` | record never written (`uiuctagRecordErased()`) | skips the record |
| pressure and temperature `0` | reading failed: conversion error, DRDY timeout, or pressure outside 300-1250 hPa (`UIUCTAG_RAW_FAILED`) | no pressure or temperature row; activity still decoded |
| every bucket `0x1F`, word `0x7FFF` | the minute before was not watched end to end | no activity rows for that record |
| bucket 21-30 | cannot be written | decoded as not observed |

Activity comes first because it makes the erased test exact without reserving
any sensor value: a written activity word has bit 15 clear, so its high byte is
never `0xFF`. The range check in `samplePressureRaw()` stores a bus that reads
all ones as a failed reading.

**Capacity.** 4,194,304 B / 8 B = 524,288 records = 16,384 blocks =
**364.1 days** of continuous collection on the 4 MiB AT25XE321D, with no
margin to a full year. Internal checkpoints are not the limit. Hibernation
writes nothing and stretches the calendar span; each hibernation abandons the
unused records of the open block.

## Time mapping

RUNNING wakes on the RTC **minute alarm** (`enableAlarm(0, ALARM_MINUTE)`),
not the free-running ticker, so wakes land on epoch minute boundaries. The
first minute wake of a run writes the first record and opens the first block,
and that instant anchors the grid:

```text
first record           = first minute alarm after RUNNING entry
record s of a block    = checkpoint.epoch + s * 60          (s = 0..31)
next block starts at   = checkpoint.epoch + 32 * 60
activity bucket b of s = record time - 60 + b * 20          (b = 0..2)
```

- `epoch` is the time of the block's own record 0, not a rounded boundary. Why
  the grid is run-anchored:
  [decision 0004](../../../../docs/decisions/0004-uiuctag-run-anchored-grid-and-lazy-block-open.md).
- Pressure and temperature are an instantaneous reading at the record time.
  The activity word covers the minute that **ends** at the record time, so the
  three buckets start at `t - 60`, `t - 40` and `t - 20`.
- **Every checkpoint maps to exactly one 256-byte block**, at
  `extern_log_block * 256`. A restart may skip records but never breaks that
  correspondence, so block index equals checkpoint index.
- Gaps can be anywhere in a block: a missed minute leaves an erased record in
  place rather than moving later ones.
- A block opens lazily: its checkpoint is written after the record-0 write it
  anchors, so every checkpoint describes at least one record.

## Write sequencing

Every minute wake at `T` does, in order:

1. Accumulate activity since the last wake into the three buckets of the
   current minute, measured from the previous record. Seconds outside that
   minute are dropped, not wrapped, and a bucket stops at 20.
2. Read the BMP585 into the record (`samplePressureRaw()`), with the sensor
   rail power-cycled. This comes before waking the flash: both share the SPI1
   bus mutex, which `dataLogWriteBegin()` holds and which is not reentrant.
3. Set the activity word: the accumulated buckets when the previous record was
   written at exactly `T - 60` with no reset between, otherwise
   `UIUCTAG_ACTIVITY_UNOBSERVED_WORD`. The first record of a run, the first
   after a reset or a missed wake, and the first after hibernation are
   therefore unobserved, never a partial count.
4. `dataLogWriteBegin()`, `dataLogWriteRecord(g, &record)`,
   `dataLogWriteEnd()`: one 8-byte page program at `8g`, where `g` is the
   global record index. Records are 8-byte aligned and 8 divides 256, so a
   program never crosses a page, and a record is either fully written or
   erased.
5. On record 0 of a new block, append the internal checkpoint.

**Cursor.** No extra persistent state: with `C = checkpoint[pages - 1]`, the
slot is `(T - C.epoch) / 60` and `g = C.extern_log_block * 32 + slot`. A reset
mid-block therefore resumes on the same record times and never reprograms a
written record; the minutes it missed stay erased. Slot 32 or later (a full
block, or a long gap such as hibernation) opens a new block anchored at `T`.
`restoreLog()` recounts checkpoints; that linear scan runs only on the
power-on, brownout and exception recovery path, not on ordinary standby wakes.

Hibernation is entered at a minute wake, after that wake's record is written;
a resumed run opens a fresh block. The run ends at its stop time before any
record is written on that wake. A full checkpoint region or external flash, or
any `LOGWRITE_ERROR` from a record or checkpoint write, ends the run with
`Finished(T_INIT, State_EVENT_INTERNALFULL)`.

## Firmware/host interface

What `data_logAck()` guarantees to the `host/libraries/tagcore/sqlitelog`
decoder:

- One `Ack.uiuctag_data_log` per 32-minute block. The download index space is
  `0 .. internal_data_count - 1`. An index with no written checkpoint returns
  `Ack_Err_NODATA`. `external_data_count` is not a block count: it is the
  global record index of the last written record plus one. `Running()` keeps
  it per record, and `restoreLog()` rebuilds it after a recovery from the
  newest checkpoint's block, reading that one block of external flash. It is
  reported in the status only; nothing bounds a download by it.
- `voltage` is volts (`vdd100 * 0.01`). `samples` is `n * 8` bytes of
  `t_UIUCTagRecord` exactly as stored, `n <= 32`, always starting at record 0,
  so the array index is the slot number. Trailing never-written records are
  trimmed; interior gaps stay. The nanopb bound is `max_size:256`.
- Record `s` is at `epoch + s * 60`; its activity bucket `b` starts at
  `epoch + s * 60 - 60 + b * 20`. Do not round `epoch`.
- The data layout version is 2 (`TAG_IDENTITY_DATA_LAYOUT_VERSION`), firmware
  2.0.0. The 12-byte five-minute layout 1 is not decoded by any host tool.
- The host skips erased records, failed readings and unobserved buckets rather
  than storing zeros. It writes the same `Voltage`, `Activity`, `Pressure` and
  `Temperature` tables as BitPresTag; activity rows are 20 s apart, in percent
  of 20 s. The schema is in the user reference,
  [`host/docs/src/reference/sqlite-logs.md`](../../../../host/docs/src/reference/sqlite-logs.md);
  the decoder rules are in
  [`host/libraries/tagcore/sqlitelog/README.md`](../../../../host/libraries/tagcore/sqlitelog/README.md).

## Testing

How each layer is verified, with and without hardware, is in
[test-strategy.md](test-strategy.md); the host simulation is in
[`../test/README.md`](../test/README.md). Hardware validation is in the
[bring-up report](investigations/bringup-report.md); power measurements are in
[power-results.md](power-results.md). The bring-up figures, and every power
entry taken on firmware before 2.0.0, are for the old five-minute format.

## History

- [Decision 0044](../../../../docs/decisions/0044-uiuctag-minute-records.md):
  minute records replace the 12-byte five-minute format.
- [proposals/minute-sampling.md](proposals/minute-sampling.md): the plan for
  minute records, built with the differences recorded in decision 0044.
- [data-collection.md](data-collection.md): the original data-collection plan
  (stages S1-S4, work items W1-W10), for the five-minute format.
- Pressure-sampling options for BMP58x parts:
  [`../../families/BitPresTag/design/bmp581-forced-mode.md`](../../families/BitPresTag/design/bmp581-forced-mode.md).
