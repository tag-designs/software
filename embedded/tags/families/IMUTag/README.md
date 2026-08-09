# IMUTag Family

Shared application code for the `IMUTagBreakout`, `IMUTagbmm350`,
`IMUTagNand`, `IMUTagU375`, and `IMUTagU3bmm350` build variants lives here.

The variants share the IMUTag data-log format, configuration handling, device
binding table, sensor orchestration, RUN-state acquisition flow, and default
ChibiOS configuration. Variant directories keep their board selection,
processor-specific makefile choice, firmware identity strings, and any
temporary bring-up overrides.

`IMUTagBreakout` uses the STM32L432 shared tag makefile and the generated
`IMUTagv1` board. `IMUTagbmm350` uses the same generated breakout board and
MX25L external flash with the AK09940A replaced by a BMM350 on the swapped
software-I2C RTC bus and PA4 as the BMM350 INT/DRDY input. `IMUTagU375` uses
the STM32U3xx/Cortex-M33 makefile, a generated STM32U375 board with the same
logical pinout, and local U375 `mcuconf.h`/linker settings. `IMUTagU3bmm350`
combines the U375 board, U3 RTC/linker settings, and MX25U12843 flash path with
the BMM350 magnetometer binding. `IMUTagNand` derives from `IMUTagU3bmm350`,
uses the generated `IMUTagNandv1` board files configured for STM32U375, and
selects the GD5F SPI-NAND storage module. The old L432 `IMUTagNandv1` firmware
member is retained as source history only.

Design notes:

- [`design/jitter-free-sampling-timing-reconstruction.md`](design/jitter-free-sampling-timing-reconstruction.md)
  plans jitter-free IMU sampling from a smooth RV-3028 reference, STM32 RTC
  smooth calibration for real-time events, and downloadable timing metadata for
  corrected reconstruction.
- [`design/internal-header-checkpoints.md`](design/internal-header-checkpoints.md)
  describes the sparse STM32U3 internal-header checkpoint scheme used to
  recover NAND-backed IMUTag external log cursors.
