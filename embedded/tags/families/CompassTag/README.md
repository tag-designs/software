---
type: readme
status: current
summary: What the CompassTag family variants share, where family-specific drivers live, and links to design notes and the host simulation.
---

# CompassTag Family

Shared application code for the `CompassTag`, `CompassTagAT25`, and
`CompassTagAT25Breakout` build variants lives here.

The variants currently share:

- ChibiOS configuration
- configuration handling
- sensor sampling, calibration flash storage, and calibration ACK handling
  selected by the family build manifest
- core power and bus control for the RTC, magnetometer, external flash SPI,
  and standby pin pulls
- a family device binding for the LIS2DU12 USART-style accelerometer path,
  including the wakeup line and wake-source selection
- shared headers for logging, persistence, sensors, and configuration

The LIS2DU12 accelerometer driver, its `tag_test_lis2du12()` hook, and its
`devices.c` binding live here because the wakeup setup and USART-style
transaction framing are currently specialized to this tag family rather than a
general accelerometer driver. The AK09940A magnetometer is a common sensor, so
CompassTag now uses the standard binding in `common/core/src/pwr.c`. Any
remaining family-specific standby and wakeup policy lives in `devices.c`
through the tag device hooks.

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

The AT25 variants also opt into DMA-backed storage block reads and writes from
their local `custom.h` files. Because all CompassTag variants share this
family's ChibiOS configuration, `cfg/mcuconf.h` must continue to define
`STM32_DMA_REQUIRED`; without it the block-transfer switches compile the DMA
path but the STM32 DMA support code is not linked. The plain `CompassTag`
variant does not currently enable the DMA block switches, even though the shared
family configuration makes DMA support available.

If a divergent source or config becomes common again, move it here and remove
the local copy from all variants. If a variant needs a temporary board-specific
power or ChibiOS experiment, add a same-named local `src/pwr.c` or `cfg/*.h`;
otherwise use the common core implementation as the single source of truth.

The CompassTag variants now use the common core state machine. Family-specific
behavior stays in the tag hooks such as `Running()`, `tagDevicesDeinit()`,
sensor orchestration, and storage/configuration bindings.

## Calibration Storage

Host-written calibration constants live in a flash log in `sensors.c`, in the
top flash page (`0x0803f800`, page 127). `family.mk` reserves that page with
`--defsym=TAG_CALIBRATION_PAGES=1`, and `STM32L432xC.ld` pins `.calibration`
there, so the table no longer moves with code size
([decision 0027](../../../../docs/decisions/0027-firmware-l432-calibration-pinned-to-top-page.md)).

`.persistent` does still move with code size, so a plain program is not a safe
upgrade. Upgrade a calibrated tag with `flash_release.py --keep-calibration`,
which erases every page below the table and keeps calibration, then reconfigure
it. An upgrade that changes the calibration record format needs `--erase` and a
recalibration instead.

## Design Documents

- [Power Test Plan](design/power-test-plan.md): the power check every
  CompassTag change should pass -- each terminal state must reach the Standby
  floor after a debugger attach. The shared rig procedure is
  [power testing](../../../../docs/bench/power-testing.md).
- [Power Results](design/power-results.md): append-only record of every
  CompassTag power measurement.
- [Standby-after-attach investigation](design/investigations/2026-09-compasstag-standby-after-attach.md):
  the 2026-09 fault that drew ~365 uA in every terminal state after an attach.
- [TODO](TODO.md): open power and monitor items.

## Host simulation

[`test/`](test/README.md) compiles the real `state_run.c` and `datalog.c` for
the host against stubs, fills a fake external flash, and checks every page
through the real `data_logAck()`. It is not part of any build.
