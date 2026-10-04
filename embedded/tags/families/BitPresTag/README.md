---
type: readme
status: current
summary: BitPresTag family variants, what UIUCTag overrides locally, and ADXL362 configuration semantics.
---

# BitPresTag Family

This family contains the shared application code for the BitPresTag pressure
and activity tags.

Current variants:

- `BitPresTag`: AT25XE external flash.
- `BitPresTagMX25R`: MX25R external flash.
- `UIUCTag`: ADXL367 on USART2 + BMP585 on SPI1 with LPS_RDY interrupt.

`BitPresTag` and `BitPresTagMX25R` differ only in the external flash module
selected from their `project.mk` files. Shared headers, state code, data logging,
tests, and ChibiOS configuration live here. Variant directories keep `custom.h`
and the small build wrappers so a flash-specific firmware string or board
constant can still be adjusted without copying application code.

`UIUCTag` is the exception: it owns tag-local `state_run.c`, `datalog.[ch]`,
`sensors.[ch]`, and `devices.[ch]` because it carries different sensors and
stores a different log record (see `include/uiuctag_log_format.h`). The common
makefile resolves a tag's `./src` and `./inc` ahead of the family directories, so
those files replace the family versions for that target only, with no change to
the shared sources. Do not "unify" them back: the record formats are genuinely
different, not accidentally divergent.

Power and bus control now come from the common `tag_core` module. The
BitPresTag variants keep any RTC line-swap override in `custom.h`; the shared
family `devices.c` owns the accelerometer wake-source selection and standby pin
policy.

## Configuration Semantics

BitPresTag keeps the ADXL362 in wake-mode operation for activity detection.
The host configuration therefore exposes only the active threshold, inactive
threshold, and inactivity count. The stored ADXL362 range, sample rate, and
anti-alias filter are fixed by firmware.

The protobuf field `Config.adxl362.inactive_sec` is historical for this tag
family. BitPresTag interprets it as an inactivity sample count at the
accelerometer wake-mode rate, not as seconds.

If a variant needs to experiment with a ChibiOS setting during bring-up, add the
same-named file under that variant's local `cfg/` directory. The common makefile
searches local `cfg/` before this family `cfg/`, so local overrides still work
without copying the normal configuration into every variant.

The larger descriptor-based power cleanup remains future work, especially for
the USART-style LPS pressure sensor bus.

## Design Notes

- [UIUCTag design](../../UIUCTag/design/overview.md): how UIUCTag's hardware
  differs from this family, and the record layout, timing and firmware/host
  contract of its log. UIUCTag owns its own copies of the files above; read
  this before changing them.
- [UIUCTag test strategy](../../UIUCTag/design/test-strategy.md): how the log
  path is verified without hardware, and what is left to a tag run.
- [UIUCTag bring-up report](../../UIUCTag/design/investigations/bringup-report.md):
  six bugs found and fixed getting a fresh UIUCTag to real logged data, and the
  extended power measurement.
- [BMP581/BMP585 forced-mode pressure](design/bmp581-forced-mode.md): the
  BMP58x forced-mode support that exists and the interrupt-paced sampling that
  does not.
