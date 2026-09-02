# IMUTag Family

Shared application code for the active `IMUTagNand` and `IMUTagNandBmp581`
build variants lives here. Older breakout, BMM350 bring-up, U375 flash, and
L432 NAND variants have been moved under `embedded/tags/archive/`.

The variants share the IMUTag data-log format, configuration handling, device
binding table, sensor orchestration, RUN-state acquisition flow, and default
ChibiOS configuration. Variant directories keep their board selection,
processor-specific makefile choice, firmware identity strings, and any
temporary bring-up overrides.

`IMUTagNand` uses the generated `IMUTagNandv1` board files configured for
STM32U375 and selects the 1 Gbit GD5F SPI-NAND plus LPS22HH pressure modules.
`IMUTagNandBmp581` uses the generated `IMUTagNandv2` board files and selects
the BMP581 pressure module plus the 2 Gbit GD5F2GM7RE SPI-NAND module while
preserving the IMUTag protocol family.

Design notes:

- [`design/jitter-free-sampling-timing-reconstruction.md`](design/jitter-free-sampling-timing-reconstruction.md)
  plans jitter-free IMU sampling from a smooth RV-3028 reference, STM32 RTC
  smooth calibration for real-time events, and downloadable timing metadata for
  corrected reconstruction.
- [`design/internal-header-checkpoints.md`](design/internal-header-checkpoints.md)
  describes the sparse STM32U3 internal-header checkpoint scheme used to
  recover NAND-backed IMUTag external log cursors.
- [`design/start-abort-diagnostics.md`](design/start-abort-diagnostics.md)
  records the intermittent abort at start, localises it to auxiliary-sensor
  init in `initDataCollection()`, and proposes a persistent per-marker detail
  word so the reason survives to the next download. The detail word uses
  STM32U3 flash-row padding, which STM32L4 markers do not have. Open.
- [`design/imutag-nand-bmp581-development-plan.md`](design/imutag-nand-bmp581-development-plan.md)
  plans the IMUTagNand replacement firmware variant that keeps the `IMUTAG`
  protocol identity while adding BMP581 pressure sensing and GD5F2GM7RE
  SPI-NAND storage.
- [`design/PowerEstimates.md`](design/PowerEstimates.md)
  records datasheet power estimates against bench measurements per sample rate,
  and compares the LDO and TPS62840 SMPS breakout builds and the NAND load
  switch against the storage-limited runtime bound.
