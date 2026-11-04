---
type: decision
status: accepted
summary: UIUCTag stores one 8-byte record a minute -- three 20-second activity counts for the minute before, then raw BMP585 pressure and temperature -- written whole in one program, 32 to a 256-byte page; the 12-byte five-minute format (layout 1) is dropped, not decoded.
---

# 0044. UIUCTag stores one 8-byte record per minute

Date: 2026-11-04

## Context

UIUCTag logged pressure and temperature every five minutes as two floats, with
five 6-bit, one-minute activity counts, in 12-byte samples: 24 to a 288-byte
block, two hours per internal checkpoint. A reading every minute was wanted
instead (user decision, 2026-11-04), and the 288-byte stride did not fit the AT25XE's 256-byte
page, so a sample could straddle two pages and was written as up to three
separate 4-byte programs with a capacitor-recharge rest after each.

The plan was the [minute-sampling proposal](../../embedded/tags/UIUCTag/design/proposals/minute-sampling.md).
No UIUCTag had been fabricated or deployed, so no log in the old format needed
to stay readable.

## Decision

1. **One record per minute.** Every RTC minute alarm takes a BMP585 reading
   and writes one 8-byte record:

   | Bytes | Field | Contents |
   | --- | --- | --- |
   | 0-1 | activity | three 5-bit counts of active seconds per 20 s, bucket `b` in bits `5b..5b+4`, bit 15 always 0 |
   | 2-4 | pressure | raw unsigned 24-bit LE, hPa = raw / 6400 |
   | 5-7 | temperature | raw signed 24-bit LE, degC = raw / 65536 |

   Thirty-two records make one 256-byte block, one AT25XE page, covering
   1920 s and anchored by one unchanged 8-byte internal checkpoint
   (`t_UIUCTagInternalLog`). The grid, lazy block opening and re-anchoring
   after hibernation of [decision 0004](0004-uiuctag-run-anchored-grid-and-lazy-block-open.md)
   are kept; only the constants change.
2. **Activity first.** The proposal put the six sensor bytes first, in the
   sensor's register order, with activity last. Activity is stored first
   instead. A written activity word always has bit 15 clear, so its high byte
   is never `0xFF`, and a record is erased exactly when its first two bytes
   read `0xFFFF` (`uiuctagRecordErased()`). No value of pressure or
   temperature needs to be reserved for that test. (An intermediate version
   stored pressure first and mapped a pressure of `0xFFFFFF` to `0xFFFFFE` to
   make a 4-byte test complete; activity first makes that unnecessary.)
3. **The whole record is written at once, with activity for the minute
   before.** The proposal kept activity for the minute *after* the reading,
   written on the next wake in one 8-byte program spanning the previous
   record's activity and the new reading, at `8g - 2`. That program crossed a
   page boundary once per block and had to split there, every record lived
   half-written for a minute, and the newest record of every log had no
   activity. Here the reading at `t` and the activity of `[t - 60, t)` are
   programmed together as one aligned 8-byte program at `8g`. 8 divides 256,
   so a record never crosses a page, and a record is either written or erased,
   never half-written.
4. **Reserved values.**
   - A failed conversion, a DRDY timeout, or a pressure outside 300-1250 hPa
     (which includes a bus that read all ones) stores 0 in both pressure and
     temperature (`UIUCTAG_RAW_FAILED`); the record and its activity are still
     written.
   - Bucket value `0x1F` (`UIUCTAG_ACTIVITY_NOT_OBSERVED`) means not observed.
     A minute the tag did not watch end to end -- the first record of a run,
     the first after a reset or missed wake, the first after hibernation -- is
     written with every bucket `0x1F`, word `0x7FFF`. Counts 21-30 cannot
     occur and are decoded as not observed, so a corrupt word never reads as
     more than 100% active.
5. **Layout 1 is dropped.** The 12-byte five-minute format is no longer decoded
   by the SQLite writer, the text writer or the capture decoder. The firmware
   is 2.0.0 and `TAG_IDENTITY_DATA_LAYOUT_VERSION` is 2. Download stays raw:
   `UIUCTagLog.samples` carries the block's records as stored, trailing erased
   records trimmed, `max_size` 256.

## Alternatives considered

- **The proposal's write sequencing** (activity for the minute after, combined
  program at `8g - 2`). Rejected for the split program, the half-written
  record and the activity-less newest record described above. Both schemes
  observe the same minutes; they differ only in which record carries each one.
- **Temperature first, as the registers are ordered.** Rejected: the erased
  test would need all six data bytes, or a separate reserved temperature value.
- **Keeping layout 1 decodable**, through a `format` field in `UIUCTagLog` and
  both layouts in the shared header, as the proposal planned. Rejected because
  no tag ever wrote a layout-1 log outside the bench, and carrying two formats
  in every decoder and check costs more than it protects.
- **Recording partly observed minutes bucket by bucket.** Not done: the
  firmware marks the whole minute unobserved. `0x1F` per bucket leaves room to
  add it later without a format change.

## Evidence

The firmware sequencer and datalog simulations
(`embedded/tags/UIUCTag/test/`) and the host format, decoder and end-to-end
checks (`host/libraries/tagcore/test/`) cover the format, the reserved values,
the reset, hibernation and reattach paths, and a full 4 MiB part: 524,288
records in 16,384 whole blocks, every one downloaded intact.

On the bench (2026-11-04, 2.496 V) firmware 2.0 runs at 0.79 µA, against
0.56 µA for the five-minute firmware, and rests at 0.16 µA as before. A
40-minute run downloaded 41 readings 60 s apart, 120 activity buckets 20 s
apart and two checkpoints 1920 s apart. Details in
[`power-results.md`](../../embedded/tags/UIUCTag/design/power-results.md).

## Consequences

- **Capacity is 364.1 days** of continuous collection on the 4 MiB
  AT25XE321D (524,288 records, 16,384 blocks), with no margin to a full year.
  Hibernation stretches the calendar span; each abandoned partial block
  shortens it slightly. Internal checkpoints are not the limit.
- Five times as many pressure conversions as the old format: running current
  rises from 0.56 to 0.79 µA. The release check's UIUCTag `run_max_ua` is
  0.8 µA (was 0.646), by user decision.
- `dataLogWriteField()`, `UIUCTAG_WRITE_REST_MS` and the
  `UIUCTAG_SAMPLE_PERIOD_SEC` override are gone; `dataLogWriteRecord()`,
  `samplePressureRaw()` and `bmp581_sample_forced_raw_blocking_device()` are
  new.
- Activity rows are 20 s apart, in percent of 20 s. The text log tag is `ACT:`.
- The design as built is in
  [`overview.md`](../../embedded/tags/UIUCTag/design/overview.md).
