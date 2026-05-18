# CompassTag Family

Shared application code for the `CompassTag`, `CompassTagAT25`, and
`CompassTagAT25Breakout` build variants lives here.

The variants currently share:

- configuration handling
- sensor sampling, calibration flash storage, and calibration ACK handling
- power and bus control for the RTC, magnetometer, accelerometer USART path,
  external flash SPI, and standby pin pulls
- shared headers for logging, persistence, sensors, and configuration

The LIS2DU12 accelerometer driver and its `tag_test_lis2du12()` hook now live
in the common `sensor_accel_lis2du12` module. The family still owns the board
power/control code for the CompassTag USART-style accelerometer path.

The variants still keep their own `custom.h`, `project.mk`, ChibiOS `cfg/`
files, and any source files that have intentionally diverged during board
bring-up. In particular, the storage module choice stays in each variant's
`project.mk`: the original `CompassTag` uses MX25R flash, while the AT25
variants use AT25XE flash. The shared family `pwr.c` uses the board-provided
`LINE_xxx` names plus the core bus-power descriptor helpers, so storage choice
can still vary by module while the family owns the common pin policy.

If a divergent source becomes common again, move it here and remove the local
copy from all variants. If a variant needs a temporary board-specific power
experiment, add a same-named local `src/pwr.c`; otherwise keep the family copy
as the single source of truth.
