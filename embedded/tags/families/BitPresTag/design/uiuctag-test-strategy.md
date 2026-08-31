# UIUCTag Test Strategy

## Why this document exists

The UIUCTag log path spans two codebases that must agree byte for byte: firmware
that programs packed records into external flash a word at a time, and host code
that reconstructs a time series from those bytes. The expensive failures are not
crashes. They are quiet disagreements — a sample attributed to the wrong minute,
an absent reading recorded as zero, a partial block decoded as a shifted one —
that produce a plausible-looking plot from wrong data.

Hardware alone is a poor instrument for finding those. A single block rollover
takes two hours of wall time, a hibernation window takes longer, and a
mid-block reset has to be provoked by hand. Worse, when a hardware capture
disagrees with expectations, the cause could be firmware sequencing, the flash
driver, the sensor, the clock, the protobuf framing, or the host decoder, and
separating them costs a day.

So the strategy is to make each layer independently answerable, leave hardware
the questions only hardware can answer, and be explicit about which is which.

## The layers

### 1. Shared format helpers

`include/uiuctag_log_format.h` holds the record layout and every decode rule:
the missing-value test, the activity bucket accessor, and the slot-to-epoch
mapping. Both sides include it verbatim.

This is the strongest structural decision in the whole design, and it needs its
own test precisely because of that. Since firmware and host share one definition,
a bug in it is a bug on both sides simultaneously, and cross-checking them
against each other cannot reveal it. Both would agree, and both would be wrong.

`host/libraries/tagcore/test/uiuctag_format_check.cc` covers the cases where the
rules are subtle rather than obvious:

- The two distinct NaN encodings that both mean "no measurement": erased flash
  reads `0xFFFFFFFF`, a failed conversion stores the canonical `0x7fc00000`.
  Testing only one would leave a real gap — this is exactly the trap
  `sqlitelog/imutag.cc` falls into by comparing against one exact encoding.
- Values that must *not* read as missing, including infinity, which shares the
  all-ones exponent.
- Bucket order and the 0 and 60 second edges, since 60 is the largest value a
  six-bit field can hold without saturating.
- Slot-to-epoch mapping for a block opened mid-window, where the checkpoint
  epoch is not the window boundary.

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

The fake flash asserts on the two faults that are worst to diagnose on a tag:

- **Programming a word twice.** NOR flash ANDs bits instead of failing, so a
  double program corrupts data silently and looks like a sensor fault later.
  This is the single most valuable assertion in the suite, because the entire
  slot-addressing design exists to make double programming impossible, and this
  is what proves it.
- **Writing while the flash is in deep power-down**, which would fail
  intermittently depending on timing.

Covered: slot addressing across block rollover; activity words written exactly
one sample period after their sample, with correct bucket counts; canonical NaN
stored on a failed conversion, distinguishable from erased; checkpoint placement
at window rollover; a mid-block reset resuming at the right slot without
re-programming and without inventing activity for the sample whose accumulation
was lost; and a hibernation window resuming into a fresh block.

### 3. Host decoder, on synthetic payloads

`host/libraries/tagcore/test/uiuctag_decoder_check.cc` builds `UIUCTagLog`
messages by hand and runs them through `SqliteTagLogWriter`.

Synthetic payloads matter here because the interesting inputs are the ones real
captures produce rarely or only after hours: an entirely erased block, an
interior failed conversion, a malformed payload length, an activity ramp
touching both edges. Waiting for hardware to produce them is not a test plan.

This layer is also why the host work landed before the firmware download path.
The decoder was known good before any firmware bytes existed, so when the tag
started emitting blocks, a discrepancy had exactly one plausible owner.

### 4. End to end, without hardware

`host/libraries/tagcore/test/uiuctag_end_to_end_check.cc` reads the flash images
the sequencer simulation wrote, applies the same trailing-trim rule
`data_logAck()` applies, and decodes them with the real host decoder.

This is the layer that catches the two halves disagreeing about the contract
rather than either half being internally wrong: slot-to-epoch mapping, whether a
payload always starts at slot 0, how a partial block is trimmed, and whether a
value the firmware considers absent is one the host omits. Neither side's own
tests can see any of that, because each is self-consistent.

The chain verified this way is: sequencer decisions, simulated flash contents,
firmware trim rule, protobuf framing, host decode, SQLite rows.

### 5. Hardware

What the layers above cannot answer, and what a tag run is therefore for:

- Real BMP585 and ADXL367 behavior: conversion timing, the DRDY poll budget,
  wake-mode activity thresholds, and whether the ADXL367 on USART2 reports
  activity as expected.
- Minute-alarm wake timing and its energy cost. The design trades five standby
  wakes per sample for exact epoch alignment; four of those five do no sensor or
  flash IO, and the cost of that trade is a measurement, not an argument.
- The capacitor-recharge rest between program cycles. `UIUCTAG_WRITE_REST_MS` is
  a starting guess; only a current probe against the real storage capacitance
  can size it.
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
`-DBUILD_TAGCORE_CHECKS=ON` and run by hand. That is a deliberate floor, not an
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

# 3. host decoder on synthetic payloads
cmake --build <build-dir> --target uiuctag_decoder_check
<build-dir>/bin/uiuctag_decoder_check

# 4. end to end on firmware-produced bytes
cmake --build <build-dir> --target uiuctag_end_to_end_check
<build-dir>/bin/uiuctag_end_to_end_check /tmp/uiuc_sim_blocks.bin
```

Per-check detail, including the SQL that states the row-level expectations, is in
[`host/libraries/tagcore/test/README.md`](../../../../../host/libraries/tagcore/test/README.md)
and [`embedded/tags/UIUCTag/test/README.md`](../../../UIUCTag/test/README.md).

## Related notes

- [UIUCTag data collection integration plan](uiuctag-data-collection.md): the
  record format, write sequencing, and staged migration these checks cover.
- [UIUCTag board integration plan](uiuctag-board-integration.md): the hardware
  bring-up sequence and device tests.
