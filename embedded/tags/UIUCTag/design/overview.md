---
type: design
status: current
summary: What UIUCTag is, how its hardware differs from BitPresTag, and the record layout, timing and firmware/host contract of its pressure and activity log.
last-verified: a87fc84a
---

# UIUCTag Design

UIUCTag is a BitPresTag-family tag that logs **pressure and temperature every
5 minutes and accelerometer activity per minute**, in 2-hour blocks of 24
twelve-byte samples. Sample times are not stored: a block's checkpoint holds
the epoch of its first slot, and slot `s` is at `epoch + s * 300`. A missing
pressure or temperature is NaN; a missing activity word is the erased value
`0xFFFFFFFF`. It reuses the family state machine, tests, storage modules
and ChibiOS configuration, and replaces the family's sensors, log format and
RUNNING state with its own.

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
  `0x51`). UIUCTag power-cycles the sensor rail for every sample and uses
  `bmp581_config_forced_fast_device()`, which writes only the registers a
  power-on-reset part needs. The conversion wait **polls** `INT_STATUS` with
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

| Quantity | Value | Source |
| --- | --- | --- |
| Activity bucket | 60 s, 6 bits | `UIUCTAG_ACTIVITY_BUCKET_SECONDS`, `UIUCTAG_ACTIVITY_BUCKET_MASK` |
| Buckets per sample | 5 | `UIUCTAG_ACTIVITY_BUCKETS_PER_EXTERNAL_BLOCK` |
| Sample period | 300 s | `UIUCTAG_EXTERNAL_BLOCK_SECONDS` |
| Samples per block | 24 | `UIUCTAG_LOG_SAMPLES` |
| Block period | 7200 s (2 h) | `UIUCTAG_DATA_LOG_SECONDS` |
| Sample record | 12 B: `float pressure` (hPa), `float temperature` (°C), `uint32_t packed_activity_data` | `t_UIUCTagSample` |
| External block stride | 288 B | `UIUCTAG_SAMPLE_BYTES_MAX` |
| Internal checkpoint | 8 B: `int32 epoch`, `uint16 vdd100` (0.01 V), `uint16 extern_log_block` | `t_UIUCTagInternalLog` |

The checkpoint is the tag's `t_DataHeader`, so the common
`core/src/persistent.c` declaration `t_DataHeader vddHeader[256]` stores it.
**The `[256]` is not the capacity**: `readDataHeader()` and `writeDataHeader()`
bound against `__persistent_end__`.

**Capacity.** About 26,880 checkpoints in internal flash (6.1 years) and
14,563 blocks in the 4 MB external flash (3.3 years); external flash binds.
Each hibernation abandons the unused slots of the open block. Figures from the
[bring-up report](investigations/bringup-report.md#maximum-run-time-from-available-memory).

## Time mapping

RUNNING wakes on the RTC **minute alarm** (`enableAlarm(0, ALARM_MINUTE)`),
not the free-running ticker, so wakes land on epoch minute boundaries. The
first wake of a run writes the first sample and opens the first block, and that
instant anchors the grid:

```text
first sample          = first minute alarm after RUNNING entry
sample s of a block   = checkpoint.epoch + s * 300
next block starts at  = checkpoint.epoch + 24 * 300
bucket b of sample s  = sample epoch + b * 60
```

- `epoch` is the time of the block's own slot 0, not a rounded boundary. Why
  the grid is run-anchored rather than aligned to multiples of 7200 s:
  [0004-uiuctag-run-anchored-grid-and-lazy-block-open.md](../../../../docs/decisions/0004-uiuctag-run-anchored-grid-and-lazy-block-open.md).
- Pressure and temperature are instantaneous readings at the slot time.
  Activity bucket `b` counts active seconds in the `b`-th minute after it,
  0..60, so the 6-bit field never saturates.
- **Every checkpoint maps to exactly one 288-byte block**, at
  `extern_log_block * 288`. A restart may skip fields or slots but never breaks
  that correspondence, so block index equals checkpoint index.
- **NaN means no measurement.** Erased flash reads `0xFFFFFFFF`, itself a NaN;
  a failed sensor read stores the canonical `__builtin_nanf("")`. An activity
  word of `0xFFFFFFFF` was never written. Gaps can be anywhere in a block.
- A block opens lazily: its checkpoint is written after the slot-0 sample it
  anchors, so every checkpoint describes at least one sample.

## Write sequencing

Four of every five minute wakes only account activity. On a sample wake the
tag performs at most three 4-byte programs through
`dataLogWriteField(sample_index, field_offset, word)`, each followed by a
`UIUCTAG_WRITE_REST_MS` (default 20 ms) `stopMilliseconds()` sleep so the
storage capacitor recharges:

1. `packed_activity_data` of the **previous** sample, at `g_prev * 12 + 8`;
2. `pressure` of the current sample, at `g * 12 + 0`;
3. `temperature` of the current sample, at `g * 12 + 4`;

where `g` is the global sample index. `dataLogWriteBegin()` / `End()` wake and
sleep the flash once around the group. The previous sample's activity write is
suppressed when that sample was not in the immediately preceding window (first
sample of a run, after hibernation, after a reset), so a partial count is never
recorded as complete. Activity is credited to bucket
`(i - lastwrite) / 60`; seconds beyond the fifth bucket are dropped, not
wrapped.

**Cursor.** No extra persistent state: at a minute wake at time `T`, with
`C = checkpoint[pages - 1]`, a sample is due when `(T - C.epoch) % 300 == 0`,
the slot is `(T - C.epoch) / 300`, and `g = C.extern_log_block * 24 + slot`. A
reset mid-block therefore resumes on the same sample times and never
reprograms a written slot. A sample is taken only on a wake that lands on the
current grid, so slot 24 or later (a full block, or a long gap such as
hibernation) opens a new block at the first on-grid wake `T`. After a gap that
ends off the grid, nothing is sampled until the next on-grid minute, up to four
minutes later, and the new block keeps the old grid's phase. `restoreLog()` recounts checkpoints;
that linear scan runs only on the power-on, brownout and exception recovery
path, not on ordinary standby wakes.

Hibernation is entered only on a sample boundary; a resumed run opens a fresh
block. A full checkpoint region or external flash, or any `LOGWRITE_ERROR`
from a sample or checkpoint write, ends the run with
`Finished(T_INIT, State_EVENT_INTERNALFULL)`.

## Firmware/host interface

What `data_logAck()` guarantees to the `host/libraries/tagcore/sqlitelog`
decoder:

- One `Ack.uiuctag_data_log` per 2-hour block. The download index space is
  `0 .. internal_data_count - 1`. An index with no written checkpoint returns
  `Ack_Err_NODATA`. `external_data_count` is not a block count: during RUNNING
  it is the global sample index of the last written sample plus one, and
  `restoreLog()` seeds it with the checkpoint count after a recovery until the
  next sample write corrects it.
- `voltage` is volts (`vdd100 * 0.01`). `samples` is `n * 12` bytes of
  `t_UIUCTagSample`, `n <= 24`, always starting at slot 0, so the array index
  is the slot number. Trailing never-written slots are trimmed; interior gaps
  stay. A final block cut short by the end of flash is still served.
- Sample `s` is at `epoch + s * 300`, its activity bucket `b` covers
  `epoch + s * 300 + b * 60`. Do not round `epoch` to a 2-hour boundary.
- The host skips NaN fields and erased activity words rather than storing
  zeros. It writes the same `Voltage`, `Activity`, `Pressure` and
  `Temperature` tables as BitPresTag. The schema is in the user reference,
  [`host/docs/src/reference/sqlite-logs.md`](../../../../host/docs/src/reference/sqlite-logs.md);
  the decoder rules are in
  [`host/libraries/tagcore/sqlitelog/README.md`](../../../../host/libraries/tagcore/sqlitelog/README.md).

## Testing

How each layer is verified, with and without hardware, is in
[test-strategy.md](test-strategy.md); the host simulation is in
[`../test/README.md`](../test/README.md). Hardware validation and power
measurements are in the [bring-up report](investigations/bringup-report.md).

## History

- [data-collection.md](data-collection.md): the data-collection plan (stages
  S1-S4, work items W1-W10), now built.
- Pressure-sampling options for BMP58x parts:
  [`../../families/BitPresTag/design/bmp581-forced-mode.md`](../../families/BitPresTag/design/bmp581-forced-mode.md).
