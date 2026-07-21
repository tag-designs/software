# IMUTag Family

Shared application code for the `IMUTagBreakout`, `IMUTagbmm350`,
`IMUTagNandv1`, `IMUTagU375`, and `IMUTagU3bmm350` build variants lives here.

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
the BMM350 magnetometer binding.

`IMUTagNandv1` uses the generated `IMUTagNandv1` STM32L432 board, GD5F1GQ5RE
1 Gbit SPI NAND flash, LSM6DSV16X IMU, LPS22HH pressure sensor, and BMM350
magnetometer. The RTC and BMM350 share the hardware I2C1 bus, while the LSM6DSV16X
and NAND flash share the generated `LSM_FLASH_*` SPI1 signals.
