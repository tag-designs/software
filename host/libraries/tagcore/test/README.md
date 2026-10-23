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
framework, because this repository has no test harness to register with. Build
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

Covers the two NaN encodings that both mean "missing", values that must not be
mistaken for missing (including infinity), activity bucket order and its 0 and
60 second edges, and slot-to-epoch mapping for a block opened mid-window.

## `uiuctag_decoder_check`

Feeds hand-built `UIUCTagLog` messages through `SqliteTagLogWriter`, covering
what real captures produce only rarely or late: a full block, a partial block, an
entirely erased block, an interior failed conversion, an activity ramp touching
both edges, a malformed payload length, and an ACK with no log payload, which is
how a tag signals end of log.

Row-level expectations, against `uiuctag_decoder_check.db3`:

```sh
sqlite3 uiuctag_decoder_check.db3 \
  "SELECT count(*) FROM Pressure;"   # 28: 23 of 24 in block A, plus 5 in block B
sqlite3 uiuctag_decoder_check.db3 \
  "SELECT count(*) FROM Activity;"   # 140: 5 buckets x 28 sampled slots
sqlite3 uiuctag_decoder_check.db3 \
  "SELECT count(*) FROM Pressure WHERE Epoch = 1767227700;"  # 0, failed sample
sqlite3 uiuctag_decoder_check.db3 \
  "SELECT Activity FROM Activity WHERE Epoch < 1767225900 ORDER BY Epoch;"
  # 0, 25, 50, 75, 100 percent
```

The fully erased block contributes a `Voltage` row and nothing else, which is
correct: its checkpoint was written, its samples were not.

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
slot-to-epoch mapping, whether a payload starts at slot 0, how a partial block is
trimmed, and whether a value the firmware calls absent is one the host omits.
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
