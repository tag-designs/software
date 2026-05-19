# CompassTag Family

Shared application code for the `CompassTag`, `CompassTagAT25`, and
`CompassTagAT25Breakout` build variants lives here.

The variants currently share:

- ChibiOS configuration
- configuration handling
- sensor sampling, calibration flash storage, and calibration ACK handling
- power and bus control for the RTC, magnetometer, external flash SPI, and
  standby pin pulls
- a family device binding for the LIS2DU12 USART-style accelerometer path
- shared headers for logging, persistence, sensors, and configuration

The LIS2DU12 accelerometer driver, its `tag_test_lis2du12()` hook, and its
`devices.c` binding live here because the wakeup setup and USART-style
transaction framing are currently specialized to this tag family rather than a
general accelerometer driver. The AK09940A magnetometer is a common sensor; the
core power code now has a standard AK09940A binding, while this family `pwr.c`
keeps a strong binding until the remaining CompassTag-specific power file can
be retired.

The variants still keep their own `custom.h`, `project.mk`, and any source or
configuration files that have intentionally diverged during board bring-up. In
particular, the storage module choice stays in each variant's `project.mk`: the
original `CompassTag` uses MX25R flash, while the AT25 variants use AT25XE
flash. The shared family `pwr.c` uses the board-provided `LINE_xxx` names plus
the core bus-power descriptor helpers, so storage choice can still vary by
module while the family owns the common pin policy.

If a divergent source or config becomes common again, move it here and remove
the local copy from all variants. If a variant needs a temporary board-specific
power or ChibiOS experiment, add a same-named local `src/pwr.c` or `cfg/*.h`;
otherwise keep the family copy as the single source of truth.
