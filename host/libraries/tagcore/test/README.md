---
type: readme
status: current
summary: How to build and run the tagcore offline assertion checks — UIUCTag format, decoder and end-to-end, and ConfigShape.
---

# tagcore Offline Checks

Assertion programs that drive tagcore code with synthetic input, so it can be
developed and regression checked with no tag attached: the log decoders with
synthetic payloads, and `ConfigShape` with the embedded default configs.

They are plain `main()` programs with `assert()`, not a registered test
framework, because this repository has no test harness to register with. The
UIUCTag checks `#undef NDEBUG` before their includes, so their assertions stay
live in a Release build, where CMake defines `NDEBUG`; before that, a Release
build compiled them out and the checks passed without checking anything. Build
them explicitly:

```sh
cmake -DBUILD_TAGCORE_CHECKS=ON <build-dir>
cmake --build <build-dir> --target uiuctag_format_check uiuctag_decoder_check \
      uiuctag_end_to_end_check config_shape_check
```

Each writes its output into the working directory by default and takes optional
path arguments.

## `uiuctag_format_check`

Exercises the shared helpers in `include/uiuctag_log_format.h`. That header is
the single definition of the record layout and decode rules for *both* firmware
and host, which is its main virtue and also why it needs direct testing: a
mistake there is a mistake on both sides at once, and cross-checking the two
sides against each other would not reveal it.

Covers the record size and field byte order (activity, pressure,
temperature), the erased test on the activity word and that no written record
passes it, the failed-reading value, 24-bit sign extension and the
conversions to hPa and degrees C, activity bucket order with its 0, 20,
not-observed and invalid (21-30) values, and record and bucket epochs for a
block anchored at a minute rather than a block boundary, including that the
last bucket ends exactly at the record's reading.

## `uiuctag_decoder_check`

Feeds hand-built `UIUCTagLog` messages through `SqliteTagLogWriter`, covering
what real captures produce only rarely or late: a full block with an unobserved
first minute, an interior failed reading and an interior erased record; a
partial block; an entirely erased block; malformed and over-long payloads; and
an ACK with no log payload, which is how a tag signals end of log. It then
opens the database it wrote (by default `uiuctag_decoder_check.db3`) and
asserts the row counts per table,
that a failed reading leaves no pressure row but keeps its activity, the
activity percentages and their epochs in the minute before each record, that
an unobserved minute has no rows, and the unit conversions. Expect
`uiuctag_decoder_check: all assertions passed`.

The fully erased block contributes a `Voltage` row and nothing else, which is
correct: its checkpoint was written, its records were not.

## `uiuctag_end_to_end_check`

Decodes the flash images produced by
`embedded/tags/UIUCTag/test/sequencer_sim.c`, applying the same trailing-trim
rule `data_logAck()` applies, so the firmware-to-SQLite chain is checked without
hardware:

```sh
/tmp/sequencer_sim blocks.bin
./uiuctag_end_to_end_check blocks.bin e2e.db3
```

This is the check that catches the two halves disagreeing about the contract —
record-to-epoch mapping, whether a payload starts at record 0, how a partial
block is trimmed, and whether a value the firmware calls absent is one the host
omits. It also asserts every decoded block against the simulated run: a reading
every minute, and three activity rows for every record whose minute was
observed.
Neither side's own tests can see those.

See the
[UIUCTag test strategy](../../../../embedded/tags/UIUCTag/design/test-strategy.md)
for how these fit together and what remains for a hardware run.

## `config_shape_check`

Checks `ConfigShape` (`../configshape.h`). It takes no arguments and prints
`CONFIG SHAPE CHECK: all assertions passed`. It asserts that:

- every embedded default config loads;
- for every tag type, the shape gives exactly the field visibility that the
  retired build-time generator (`cmake/GenerateConfigVisibility.cmake`)
  produced. Its last output is frozen in the check as the reference;
- `has()` and `isConfigPath()` tell a field a tag lacks from a name that is no
  field at all;
- `fromJson()` keeps zero-valued keys, accepts lowerCamelCase names and rejects
  unknown keys, and `forFile()` limits a file to its own tag type's fields;
- `merge()` copies a stated zero, keeps fields the file leaves out, never adds
  a field the tag lacks, never copies `tag_type`, and copies `hibernate` only up
  to the tag's own count.

A default config edited to drop or add a field will fail the frozen-table
comparison. That is intended: decide whether the change is wanted, then update
the table in the check.
