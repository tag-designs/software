---
type: readme
status: current
summary: UIUCadxl367 is the UIUCTag firmware for boards fitted with an ADXL367 instead of the ADXL362; it builds UIUCTag's sources and owns only its firmware string.
---

# UIUCadxl367

The UIUCTag board fitted with an **ADXL367** accelerometer. The UIUCTag
prototypes carry the pin-compatible **ADXL362**, which the `UIUCTag` target
drives; build this target only for a board with an ADXL367 on it. Flashing the
wrong one fails `tag-test`'s `RUN_ADXL362` self-test, which checks the part ID.

Everything except the accelerometer is UIUCTag's: the board (`board-uiuctag`),
protocol (`uiuctag_proto`), external-flash loader (`AT25XE_UIUCTag`), log
format, stored configuration and tag type (`UIUCTAG`). The host tells the two
apart only by `Config.adxl362.accel_type`, which this firmware reports as
`AdxlType_367`. It is a development target: it is built, but not distributed
in the host tools package.

## Layout

- `project.mk` -- UIUCTag's manifest with the `sensor_accel_adxl367` module
  and `accel_adxl367.c`, and `../UIUCTag/inc` and `../UIUCTag/src` added to
  the search path ahead of the BitPresTag family.
- `inc/custom.h` -- UIUCTag's, with this target's firmware string.

There are no sources here. The accelerometer setup is
`../UIUCTag/src/accel_adxl367.c`; how the two targets share everything else is
in the [UIUCTag design](../UIUCTag/design/overview.md#source-ownership). A
change to `../UIUCTag` changes this target too, so build both.
