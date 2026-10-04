---
type: investigation
status: closed
summary: First bench validation of the IMUTagNandBmp581 target on the BMP581 breakout -- NAND and BMP581 self-tests and a short 400 Hz collection.
---

# IMUTagNandBmp581 First Breakout Bring-Up (2026-08)

Cut verbatim from the *Bring-Up Status* section of
[`embedded/tags/IMUTagNandBmp581/README.md`](../../../../IMUTagNandBmp581/README.md).
The section carries no date; 2026-08 is when the target and its README were
added (commit 723c5d21, 2026-08-28). The closing paragraph about `debug_log` is
stale: the module is no longer in the target's module list, and
`project.mk` warns that it breaks Stop 3 entry on the STM32U375.

Bench validation on the first BMP581 breakout confirmed:

- `RUN_EXT_FLASH` passes against the GD5F2GM7RE SPI-NAND after using the
  shared GD5F self-test path for both 1 Gbit and 2 Gbit variants.
- `RUN_LPS` passes against BMP581 after correcting the PA9/PA10
  data-ready/chip-select assignment.
- A short 400 Hz IMU collection produced SQLite `ImuAccel`, `ImuGyro`,
  `ImuMag`, `ImuPressure`, `ImuTemperature`, and `Calibration` tables with
  plausible pressure and pressure-temperature values.

The `debug_log` module remains enabled while this target is under bring-up so
BMP581 and NAND diagnostics can be captured by `tag-test --debug` and
qtmonitor.
