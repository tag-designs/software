---
type: design
status: current
summary: How the UIUCTag log path is tested layer by layer, from shared format helpers and host simulation to hardware, and what it does not cover.
---

# UIUCTag Test Strategy

## Why this document exists

The UIUCTag log path spans two codebases that must agree byte for byte: firmware
that programs one 8-byte record a minute into external flash, and host code
that reconstructs a time series from those bytes. The expensive failures are not
crashes. They are quiet disagreements — a reading attributed to the wrong minute,
activity credited to the wrong 20 seconds, an absent reading recorded as zero, a
partial block decoded as a shifted one — that produce a plausible-looking plot
from wrong data.

Hardware alone is a poor instrument for finding those. A single block rollover
takes 32 minutes of wall time, a hibernation window takes longer, and a
mid-block reset has to be provoked by hand. Worse, when a hardware capture
disagrees with expectations, the cause could be firmware sequencing, the flash
driver, the sensor, the clock, the protobuf framing, or the host decoder, and
separating them costs a day.

So the strategy is to make each layer independently answerable, leave hardware
the questions only hardware can answer, and be explicit about which is which.

## The layers

### 1. Shared format helpers

`include/uiuctag_log_format.h` holds the record layout and every decode rule:
the erased and failed-reading tests, the raw-to-unit conversions, the activity
bucket accessor, and the record-to-epoch mapping. Both sides include it
verbatim.

This is the strongest structural decision in the whole design, and it needs its
own test precisely because of that. Since firmware and host share one definition,
a bug in it is a bug on both sides simultaneously, and cross-checking them
against each other cannot reveal it. Both would agree, and both would be wrong.

`host/libraries/tagcore/test/uiuctag_format_check.cc` covers the cases where the
rules are subtle rather than obvious:

- The erased test reads only the activity word, which is exact because a
  written word has bit 15 clear. Checked against a written record whose
  pressure and temperature are all ones and whose activity is the unobserved
  word `0x7FFF`.
- A failed reading (`0` in pressure and temperature) is a written record with
  no reading but valid activity, distinct from erased.
- 24-bit sign extension of temperature at its extremes, and the conversions to
  hPa and degrees C.
- Bucket order, the 0 and 20 second edges, the not-observed value `0x1F`, and
  counts 21-30, which must read as not observed rather than above 100%.
- Record and bucket epochs for a block anchored at a minute rather than a block
  boundary, and that a record's last bucket ends exactly at its reading.

The header is also compiled as C11, C++17, and ARM C11 under `-Wall -Wextra
-Wdouble-promotion`, because a header shared across three compilers is where
portability breaks silently.

### 2. Firmware sequencer, in simulation

`embedded/tags/UIUCTag/test/sequencer_sim.c` compiles the real `state_run.c` for
the host against minimal stubs and drives `Running()` over a synthetic
minute-alarm timeline.

This is possible only because of a structural property worth preserving: sensor
access lives behind `sensors.h`, and log IO behind `datalog.h`, so the sequencer
contains almost nothing but arithmetic over the acquisition clock. If a future
change reaches into a driver from `state_run.c`, this layer stops working — treat
that as a design signal, not a testing inconvenience.

The fake flash asserts on the faults that are worst to diagnose on a tag:

- **Programming a byte twice.** NOR flash ANDs bits instead of failing, so a
  double program corrupts data silently and looks like a sensor fault later.
  This is the single most valuable assertion in the suite, because the entire
  record-addressing design exists to make double programming impossible, and
  this is what proves it.
- **A write that is not one whole, aligned record**, which is what keeps every
  program inside one page.
- **Writing while the flash is in deep power-down**, which would fail
  intermittently depending on timing.

Covered: one record a minute and record addressing across block rollover;
checkpoints exactly 1920 s apart; activity for the minute before each record,
with correct 20-second bucket counts; the first minute of a run stored as not
observed; the failure value stored on a failed reading, in a written record
that keeps its activity; the stop time; a mid-block reset resuming at the right
record without re-programming, leaving the missed minute erased and storing the
first minute after it as not observed rather than a partial count; a
hibernation window resuming into a fresh block; and a reattach re-arming the
minute alarm.

`embedded/tags/UIUCTag/test/datalog_sim.c` links the real `datalog.c` as well
and runs the minute alarm until a 4 MiB and an 8 MiB part are full, then
downloads every block through the real `data_logAck()`: 524,288 records, 364
days, on 4 MiB, each checked against what was written.

### 3. Host decoder, on synthetic payloads

`host/libraries/tagcore/test/uiuctag_decoder_check.cc` builds `UIUCTagLog`
messages by hand and runs them through `SqliteTagLogWriter`.

Synthetic payloads matter here because the interesting inputs are the ones real
captures produce rarely or only after hours: an entirely erased block, an
interior failed reading or erased record, an unobserved minute, a malformed
payload length, activity at both edges. The check then queries the database it
wrote and asserts the rows itself. Waiting for hardware to produce them is not a test plan.

This layer is also why the host work landed before the firmware download path.
The decoder was known good before any firmware bytes existed, so when the tag
started emitting blocks, a discrepancy had exactly one plausible owner.

### 4. End to end, without hardware

`host/libraries/tagcore/test/uiuctag_end_to_end_check.cc` reads the flash images
the sequencer simulation wrote, applies the same trailing-trim rule
`data_logAck()` applies, and decodes them with the real host decoder.

This is the layer that catches the two halves disagreeing about the contract
rather than either half being internally wrong: record-to-epoch mapping, whether
a payload always starts at record 0, how a partial block is trimmed, and whether a
value the firmware considers absent is one the host omits. Neither side's own
tests can see any of that, because each is self-consistent.

The chain verified this way is: sequencer decisions, simulated flash contents,
firmware trim rule, protobuf framing, host decode, SQLite rows.

### 5. Hardware

What the layers above cannot answer, and what a tag run is therefore for:

- Real BMP585 and ADXL367 behavior: conversion timing, the DRDY poll budget,
  wake-mode activity thresholds, and whether the ADXL367 on USART2 reports
  activity as expected.
- Minute-alarm wake timing and its energy cost. Every minute wake now takes a
  BMP585 reading and programs one record, so the per-minute energy sets the
  deployment's life, and that is a measurement, not an argument.
- Whether one 8-byte program per wake, with no recharge rest, is within what
  the storage capacitor supplies.
- Whether the acquisition clock stays aligned across long runs, standby cycles,
  and RTC correction.
- Flash-full and checkpoint-full termination on real geometry.
- A `uiuctag.db3` fixture for `host/docs/fixtures/sensorviz`, which by policy
  comes from a real download.

## What this strategy does not do

It does not assert that the tag measures pressure correctly, and no amount of
simulation could. It verifies that whatever the sensor reports is stored in the
right slot, at the right time, and reconstructed on the host as the same value or
as an explicit gap. Sensor correctness is the device tests' job (`RUN_ADXL362`,
`RUN_LPS`) and ultimately a calibration question.

It also does not run automatically. There is no test framework in this
repository to register with, so these are assertion programs built behind
`-DBUILD_TAGCORE_CHECKS=ON` and run by hand. The host checks `#undef NDEBUG`,
so their assertions stay live in a Release build. That is a deliberate floor, not an
aspiration: the checks are cheap to run and abort with a message naming the
violated expectation. If this repository later grows a CI test target, all four
are already exit-code clean and would need only registration.

## Running everything

```sh
# 1. shared helpers, and the C/C++/ARM compile check
cmake -DBUILD_TAGCORE_CHECKS=ON <build-dir>
cmake --build <build-dir> --target uiuctag_format_check
<build-dir>/bin/uiuctag_format_check

# 2. firmware sequencer, and dump its flash images
cd embedded/tags/UIUCTag
cc -std=c11 -Wall -Wextra -o /tmp/sequencer_sim test/sequencer_sim.c \
   -Itest/stub -Itest -I../../../include -Iinc -Isrc
/tmp/sequencer_sim /tmp/uiuc_sim_blocks.bin
# ...and fill a whole part through the real datalog.c
cc -std=c11 -Wall -Wextra -Wno-pointer-to-int-cast -O1 \
   -o /tmp/uiuctag_datalog_sim test/datalog_sim.c \
   -Itest/stub -Itest -I../../../include -Iinc -Isrc
/tmp/uiuctag_datalog_sim

# 3. host decoder on synthetic payloads
cmake --build <build-dir> --target uiuctag_decoder_check
<build-dir>/bin/uiuctag_decoder_check

# 4. end to end on firmware-produced bytes
cmake --build <build-dir> --target uiuctag_end_to_end_check
<build-dir>/bin/uiuctag_end_to_end_check /tmp/uiuc_sim_blocks.bin
```

Per-check detail is in
[`host/libraries/tagcore/test/README.md`](../../../../host/libraries/tagcore/test/README.md)
and [`embedded/tags/UIUCTag/test/README.md`](../test/README.md).

## Related notes

- [UIUCTag design](overview.md): the record format, time mapping, write
  sequencing and firmware/host contract these checks cover.
- [UIUCTag bring-up report](investigations/bringup-report.md): the hardware
  runs and power measurements, on the earlier five-minute format.
