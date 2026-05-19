# CompassTag Family

Shared application code for the `CompassTag`, `CompassTagAT25`, and
`CompassTagAT25Breakout` build variants lives here.

The variants currently share:

- ChibiOS configuration
- configuration handling
- sensor sampling, calibration flash storage, and calibration ACK handling
- core power and bus control for the RTC, magnetometer, external flash SPI,
  and standby pin pulls
- a family device binding for the LIS2DU12 USART-style accelerometer path
- shared headers for logging, persistence, sensors, and configuration

The LIS2DU12 accelerometer driver, its `tag_test_lis2du12()` hook, and its
`devices.c` binding live here because the wakeup setup and USART-style
transaction framing are currently specialized to this tag family rather than a
general accelerometer driver. The AK09940A magnetometer is a common sensor, so
CompassTag now uses the standard binding in `common/core/src/pwr.c`. Any
remaining family-specific standby pin policy lives in `devices.c` through the
`tagPrepareDevicesForStandby()` hook.

The family `sensors.c` file is not a reusable sensor driver. It is the
CompassTag application layer that decides how this tag family uses its sensors:
orientation transforms, sampling flow, calibration storage/ack handling, and
the coupling between magnetometer and accelerometer data. The name is
historical and may eventually be made more explicit, for example
`sensor_runtime.c` or `sensor_orchestration.c`.

The variants still keep their own `custom.h`, `project.mk`, and any source or
configuration files that have intentionally diverged during board bring-up. In
particular, the storage module choice stays in each variant's `project.mk`: the
original `CompassTag` uses MX25R flash, while the AT25 variants use AT25XE
flash. The shared core power code uses the board-provided `LINE_xxx` names plus
the core bus-power descriptor helpers, so storage choice can still vary by
module while the family owns only the pin policy that is genuinely specific to
the LIS2DU12/CompassTag layout.

If a divergent source or config becomes common again, move it here and remove
the local copy from all variants. If a variant needs a temporary board-specific
power or ChibiOS experiment, add a same-named local `src/pwr.c` or `cfg/*.h`;
otherwise use the common core implementation as the single source of truth.
