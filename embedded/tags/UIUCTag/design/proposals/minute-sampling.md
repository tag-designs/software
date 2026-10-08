---
type: proposal
status: proposed
summary: Proposal to change UIUCTag to a pressure/temperature reading every minute, stored as one 8-byte record (6 raw BMP585 bytes plus three 5-bit, 20-second activity counts), with an internal checkpoint every 32 minutes anchoring one 256-byte external page.
---

# UIUCTag Minute Sampling with 8-Byte Records

**Proposal.** UIUCTag would take a pressure and temperature reading **every
minute** and store it as **one 8-byte record**: the 6 raw BMP585 data bytes,
then 2 bytes holding three 5-bit active-second counts, one per 20 seconds, for
the minute that follows the reading. An internal-flash checkpoint is written
**every 32 minutes**, and each checkpoint anchors one 256-byte external block
(32 records × 8 bytes), which is exactly one AT25XE page.

A record is filled across two wakes: pressure and temperature at the start of
its minute, activity at the end. In flash, one record's activity and the next
record's pressure and temperature are adjacent, so **each wake still writes
both with a single 8-byte program**. The exception is a block boundary, once
every 32 minutes, where the 8 bytes span two pages and take two programs.

The costs are a **364-day** external-flash ceiling, down from 3.3 years, and
five times as many pressure conversions. The power cost is the main risk, and
the first build step is to measure it (see [Power](#power)).

The design this replaces is in [../overview.md](../overview.md). Section
references below ("Write sequencing", "Cursor") are to that document.

## What changes

| Quantity | Now ([overview](../overview.md#record-layout)) | Proposed |
| --- | --- | --- |
| Pressure/temperature period | 300 s | **60 s** |
| Pressure/temperature storage | two `float`s (8 B), temperature through an `int16` centi-°C step | **6 raw bytes**, as read from BMP585 `0x1D`–`0x22` |
| Activity bucket | 60 s, 6 bits, 0–60 | **20 s, 5 bits, 0–20, `0x1F` = not observed** |
| Activity window of a record | the 5 minutes *after* the sample | the 1 minute *after* the sample (same direction as now) |
| Record | 12 B, fields written on two different wakes | **8 B**: pressure/temperature at the start of its minute, activity at the end |
| Records per block | 24 | **32** |
| Block | 288 B / 7200 s | **256 B (one page) / 1920 s** |
| External programs per wake | 0 on four wakes out of five; up to 3 on the fifth | **1**, or 2 at a block boundary (once per 32 wakes) |
| Checkpoint | 8 B `t_UIUCTagInternalLog` | **unchanged layout**, one per 32 minutes |

## Record format

```c
#pragma pack(push, 1)
typedef struct {
    uint8_t  temperature[3]; /* BMP585 TEMP_DATA_XLSB..MSB, signed 24-bit LE  */
    uint8_t  pressure[3];    /* BMP585 PRESS_DATA_XLSB..MSB, unsigned 24-bit LE */
    uint16_t activity;       /* bucket b (0..2) in bits 5b..5b+4; bit 15 = 0  */
                             /* written one wake after the other six bytes    */
} t_UIUCTagRecord;           /* 8 bytes */
#pragma pack(pop)
```

The six bytes are in the sensor's register order (temperature first), so a
single 6-byte burst read from `BMP5_REG_TEMP_DATA_XLSB` fills the start of the
record with no rearranging. The BMP585 data registers already hold compensated
values; "raw" means unconverted, not uncompensated. The host converts them,
using the scaling in the vendor `bmp5_get_sensor_data()`:

```text
T_raw  = sign_extend_24(t[0] | t[1] << 8 | t[2] << 16)   ->  degC = T_raw / 65536
P_raw  = p[0] | p[1] << 8 | p[2] << 16                   ->  hPa  = P_raw / 6400
```

A side benefit: temperature keeps its full 1/65536 °C resolution. Today it is
rounded to 0.01 °C by `bmp581_centi_c()` before being stored as a float.

### Missing values

Erased NOR flash reads `0xFF`. A record is written in two parts on two wakes,
so each part has its own "never written" value, and the decoder judges them
independently.

| Field | Value | Meaning | Decoder action |
| --- | --- | --- | --- |
| pressure | `0xFFFFFF` | Pressure and temperature never written | Skip them |
| pressure | `0x000000` | Wake happened, sensor read failed | Skip pressure and temperature |
| `activity` | `0xFFFF` | Activity never written | Skip activity |
| `activity` bucket | `0x1F` | Reserved: bucket not observed | Skip that bucket |
| both parts erased | | Record never written | Skip the record |

The newest record of a log normally has pressure and temperature but erased
activity, because its minute had not ended when the log stopped growing. That
is the normal state of the newest record, not a fault, exactly as in today's
format.

A failed conversion, a DRDY timeout, or a raw pressure outside
`[300 × 6400, 1250 × 6400]` (the BMP585's 300–1250 hPa range) is stored as
`P = T = 0x000000`. Neither `0` nor `0xFFFFFF` (2621 hPa) can be a real
reading, and the range check also catches a bus fault that reads all ones,
which would otherwise look like erased flash.

### Activity

Each 20-second bucket holds 0–20 active seconds in 5 bits, so **no count is
capped**: a fully active bucket reads 20 (100%). An activity word is written
only when the tag watched the whole minute, by the same rule as today: the
previous record was taken at `T − 60` with no reset in between. A minute
interrupted by a reset or a missed wake leaves its activity erased.

That rule means the firmware never has to write `0x1F` or a word of `0xFFFF`.
Both stay reserved:

- `0x1F` is kept free for recording partly observed minutes bucket by bucket,
  should that ever be wanted. It would need retained flags saying which buckets
  were watched (see [Retained state](#retained-state));
- bit 15 is always written as 0. That costs nothing, and it keeps any written
  word distinct from `0xFFFF` even if `0x1F` is later used;
- values 21–30 cannot occur. The decoder treats them as corrupt and skips the
  bucket, so a bad word is never reported as activity above 100%.

The host emits `count × 100 / 20` percent for each observed bucket, into the
existing `Activity` table. The schema does not change; there are three times
as many rows, 20 seconds apart.

## Timing

The grid, lazy block opening and cursor-from-checkpoint rules of
[decision 0004](../../../../../docs/decisions/0004-uiuctag-run-anchored-grid-and-lazy-block-open.md)
all stay; only the constants change.

```text
record s of a block     = checkpoint.epoch + s * 60          (s = 0..31)
next block starts at    = checkpoint.epoch + 32 * 60
activity bucket b of s  = record time + b * 20               (b = 0..2)
```

- **Every RTC minute alarm is a sample wake.** `on_sample_grid()` is then
  always true for a minute-aligned wake, and the separate activity-only wake
  goes away. `UIUCTAG_SAMPLE_PERIOD_SEC` can no longer be shortened (the minute
  alarm is the shortest period), so the override and its `#warning` should be
  removed.
- **Activity follows the sample, as it does now.** The record taken at `T`
  carries activity for `[T, T + 60)`, written at the wake at `T + 60`. The
  first record of a run therefore has a full minute of activity. Activity
  between entering `RUNNING` and the first minute boundary is discarded, as it
  is today.
- A block still opens lazily. Its checkpoint is written after its slot-0
  record, and a reset mid-block resumes on the same grid without reprogramming
  anything. With a 32-minute block, a hibernation wastes on average 16 records
  (8 minutes of capacity) instead of 12 records (an hour).

### Retained state

No new retained state. `pState->activity` holds the 15-bit word being
accumulated and `pState->lastwrite` decides whether it is due, as today.
Recording partly observed minutes with `0x1F` would need one more fact, which
buckets were watched. That would fit in bits 16–18 of the same `uint32_t`, so
the BitPresTag family `pState` layout would not have to change.

## Write sequencing

Per minute wake at `T`, for global record index `g`, in this order (the order
of the first two steps is required, for the SPI1 bus-mutex reason documented in
`state_run.c`):

1. Burst-read the BMP585 into a 6-byte buffer, with the rail power-cycled as it
   is today.
2. `dataLogWriteBegin()`, then program:
   - **activity is due** (record `g − 1` was taken at `T − 60`): the 8 bytes
     `activity(g − 1) ‖ temperature(g) ‖ pressure(g)` as **one program at
     `8g − 2`**. When `g` is slot 0 of a block, `8g` is a page boundary and
     AT25XE page programs wrap within a page, so this splits into 2 bytes at
     `8g − 2` and 6 bytes at `8g`;
   - **activity is not due** (first record of a run, after a reset, after a
     missed wake): 6 bytes at `8g`.

   Then `dataLogWriteEnd()`.
3. On slot 0 of a new block, write the internal checkpoint.

`flush_activity()` stays, but it is folded into the same program as the new
record. Programming one page in several parts on different wakes is
what the shipped firmware already does, at about twice this rate: up to three
4-byte fields per 12-byte sample, roughly 64 programs per 256 bytes, against
about 32 here.

`UIUCTAG_WRITE_REST_MS` lets the storage capacitor recover *between* program
cycles, so it applies only on the block-boundary wake, which has a split
program and a checkpoint. Whether it is needed at all should be measured, not
assumed.

**Ending a segment.** As today, the last record before the stop time or a
hibernation would keep erased activity. Both events fall on a minute wake, when
the previous record's minute is complete. The wake that ends a run or enters
hibernation can therefore write that activity (2 bytes) and skip the new
sample, so every record that has pressure also has its activity.

Every other rule stays: an end-of-flash, end-of-checkpoint-region or
`LOGWRITE_ERROR` result ends the run with `State_EVENT_INTERNALFULL`, and
hibernation is entered only on a sample boundary (now any minute).

## Capacity

| Resource | Capacity | Unit | Max run time |
| --- | --- | --- | --- |
| External flash, AT25XE321D | 4,194,304 B / 8 B | 524,288 records = 16,384 blocks | **364.1 days** |
| Internal checkpoints ([bring-up figure](../investigations/bringup-report.md#maximum-run-time-from-available-memory)) | 26,880 × 8 B | 26,880 blocks × 1920 s | 597 days |

**External flash binds at 364 days of continuous collection, just short of a
full year and with no margin.** Hibernation periods write nothing and so stretch
the calendar span; abandoned partial blocks shrink it slightly. If an
unbroken year is a hard requirement, this format cannot meet it on a 4 MB part.

`extern_log_block` is `uint16_t`, and 16,384 blocks is well within it. Because
the flash size is a whole number of 256-byte blocks, the "final block cut short
by the end of flash" case in `data_logAck()` cannot happen any more. The code
stays as a guard.

## Power

All figures are from the [bring-up report](../investigations/bringup-report.md)
on board `2036354B3032500800520028` at about 2.485 V. **None of the proposed
figures has been measured.**

| Term | Now | Proposed |
| --- | --- | --- |
| Idle baseline | 0.422 µA | 0.422 µA |
| Sample event | 76 µJ every 300 s ≈ 0.10 µA | ≤ 76 µJ every 60 s ≈ **≤ 0.51 µA** |
| Undisturbed total | 0.56 µA (measured) | **≈ 0.93 µA** (upper estimate) |
| + 12–18 activity transitions/min (junco worst case, 2.4 µJ each) | ≈ 0.75–0.85 µA | **≈ 1.12–1.22 µA** |

76 µJ is the measured cost of today's sample wake: a BMP585 conversion, a flash
wake and up to three programs with rests. The proposed wake does the same
conversion and flash wake with a single program, so 76 µJ is an upper bound,
but how much the two dropped programs save is not known. The BMP585 share of
that energy has never been measured on its own.

A year on the 11 mAh cell needs an average of **1.256 µA or less**. At the
worst-case activity rate, the per-minute event therefore has to cost:

- **≤ 81 µJ** to reach one year with no margin;
- **≤ 50 µJ** to keep the ~20% margin the current design has under the same
  worst case.

**Gate:** before anything else is built, capture the isolated energy of one
proposed minute wake with the Joulescope, following
[power testing](../../../../../docs/bench/power-testing.md). If it is above
about 50 µJ, the deferred `LPS_RDY` interrupt wait, which removes the
`INT_STATUS` polling, becomes worth pursuing. It was set aside in bring-up only
because no proven hardware timeout bounds a WFI wait. That decision should be
revisited, not quietly overridden.

## Host and format identification

The host has to decode both formats: existing `fw-v0.6` logs, downloaded files
and SWD captures stay in the old layout. The block payload alone does not say
which format it is (a 24-byte payload is two old samples or three new records),
so the format has to be stated explicitly:

- **Protobuf.** Add `uint32 format = 4;` to `UIUCTagLog`. Old firmware never
  sets it, so it reads as 0 (12-byte, 300 s); the new firmware sends 2. This
  needs no new `Ack` field and no new tag type, and the 256-byte payload fits
  the existing `samples` `max_size` of 288. Regenerate `uiuctag-proto-c`.
- **Capture/identity.** Bump `TAG_IDENTITY_DATA_LAYOUT_VERSION` to 2 in
  `inc/tag_identity_family.h` and update its page, sample and period macros.
  Teach `UiucTagDecoder` in `host/libraries/tagcore/recovery/capturesource.cc`
  layout 2, keeping layout 1.
- **Shared header.** Keep the v1 definitions in
  `include/uiuctag_log_format.h` unchanged, so the host can still decode old
  logs. Add `t_UIUCTagRecord`, its geometry constants, size assertions, and the
  decode helpers (raw → hPa/°C, written/failed tests, bucket unpack, record and
  bucket epochs) as one v2 block. The firmware uses only v2.

## Files touched

| Area | File | Change |
| --- | --- | --- |
| Shared format | `include/uiuctag_log_format.h` | v2 record, geometry, helpers, assertions |
| Driver (shared) | `embedded/tags/common/sensors/pressure/src/bmp581.c` and header | **Add** a raw 6-byte forced-sample read; existing functions unchanged (IMUTagNandBmp581 uses them) |
| Firmware | `src/sensors.c`, `inc/sensors.h` | `samplePressureRaw(uint8_t raw[6])` with the range check |
| Firmware | `src/state_run.c` | 60 s period, 3 × 20 s accumulation, previous activity and new sample in one program, activity written on stop and hibernation entry |
| Firmware | `src/datalog.c`, `inc/datalog.h` | a byte-range program that splits at page boundaries in place of `dataLogWriteField()`, 256-byte block, `_Static_assert`s, `format = 2`, and trimming of trailing fully erased records in `data_logAck()` |
| Firmware | `inc/tag_identity_family.h`, `inc/custom.h` | layout version 2, new scales; firmware version |
| Proto | `proto/tagdata.proto`, `embedded/proto-c/uiuctag-proto-c/` | `format` field, regenerated |
| Host | `host/libraries/tagcore/sqlitelog/uiuctag.cc`, `txtlogs.cc` | dispatch on `format`; v2 decode |
| Host | `host/libraries/tagcore/recovery/capturesource.cc` | layout 2 |
| Tests | `embedded/tags/UIUCTag/test/`, `host/libraries/tagcore/test/` | sequencer and datalog sims; `uiuctag_format_check`, `uiuctag_decoder_check`, `uiuctag_end_to_end_check` for both formats |
| Docs, when built | `../overview.md`, `host/libraries/tagcore/sqlitelog/README.md`, a decision record in `docs/decisions/`; this file marked `historical` | |

## Verification

Following [verifying a firmware change](../../../../../docs/bench/verifying-firmware.md):

1. **Host first.** Format, decoder and end-to-end checks pass for v1 and v2
   blocks, including erased, failed-read, newest-record (activity erased),
   reserved-bucket and end-of-flash records. The firmware sequencer sim covers run start mid-minute,
   reset mid-block, hibernation resume, a missed minute, block rollover at slot 32
   (the split program), and stop or hibernation landing on a minute.
2. **Energy gate.** Measure the isolated per-minute event (see [Power](#power)).
3. **Flashing protocol, in order:** record the UUID; **erase the existing log
   with the firmware that wrote it**, because the checkpoint is the same size
   but means something different and nothing detects the change (the warning
   in `inc/datalog.h`); flash; run `tag-test`.
4. **Functional run, untouched.** Start once, attach nothing, watch a Joulescope
   trace, stop, and download. Expect pressure and temperature exactly 60 s apart,
   activity exactly 20 s apart starting at the first sample, activity present on
   the last record before the stop, and
   a new checkpoint every 1920 s.
5. **Lifecycle and release.** `tag_lifecycle_check.py` (this changes
   `state_run.c`), a `RUNNING` current over at least three blocks with nothing
   attached, and `tag_rebuild_check.py` (this changes `data_logAck()`).

## Open questions

1. **364 days.** Is a capacity just under one year of continuous collection
   acceptable for the intended deployments?
2. **Power.** If the measured minute event leaves less than the current ~20%
   one-year margin, is that acceptable, or does the `LPS_RDY` interrupt wait
   become a prerequisite?
