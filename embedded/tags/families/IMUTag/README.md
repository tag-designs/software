# IMUTag Family

Shared application code for the `IMUTagBreakout`, `IMUTagbmm350`, and
`IMUTagU375` build variants lives here.

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
logical pinout, and local U375 `mcuconf.h`/linker settings.
