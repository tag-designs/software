# Common Tag Firmware Code

This directory contains firmware code shared by tag targets. It is deliberately
split by ownership rather than by product: common runtime code is in `core`,
device families are in `sensors`, external memories are in `storage`, RTC
drivers are in `rtc`, and monitor self-tests are in `test`.

The build entry point is still `make.mk`, with module fragments under
`modules/`. A tag's `project.mk` selects modules with `TAG_MODULES`, then may
include a tag-family manifest from `../families`. The module system controls
which common sources are compiled and which `TAG_*` feature switches are
defined.

## Where To Look

- `modules/README.md`: how the makefile module system works.
- `core/README.md`: runtime, monitor, persistent state, power, and bus helpers.
- `rtc/README.md`: RTC API and RV3028 descriptor shape.
- `storage/README.md`: external flash API and chip drivers.
- `sensors/README.md`: sensor driver descriptors, shims, and bus register I/O.
- `test/README.md`: monitor-facing self-test dispatch.

## Maintenance Rules

- Prefer narrow headers such as `power.h`, `rtc_api.h`, or `sensor_io.h` over
  the legacy umbrella `app.h`.
- Keep board pin names and wiring policy in tag or family code. Shared drivers
  should take descriptors where practical.
- Keep chip command formats in the chip driver. Put only generic bus mechanics
  in `core`.
- If a driver is only correct for one tag family, keep it in that family rather
  than forcing it into `common/sensors`.
