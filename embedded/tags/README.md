# Embedded Tag Firmware

Tag firmware targets share a ChibiOS makefile scaffold from `common/make.mk`.
Each target's `project.mk` now separates two concerns:

1. `TAG_MODULES`: shared source groups from `common/modules`.
2. Optional tag-family manifests from `families/`, when multiple targets share
   application code.
3. `ALLCSRC`: tag-local application files from that target's `src` directory.

Example:

```make
TAG_MODULES += \
       protocol_nanopb \
       tag_core \
       rtc_rv3028 \
       flash_at25xe

include ../common/modules/modules.mk
include ../families/SomeTagFamily/family.mk

ALLCSRC += \
       datalog.c \
       state_run.c
```

The module fragments are a first step toward making the shared tag runtime more
understandable. Common runtime code is being moved out of the flat `common/src`
and `common/inc` directories into subsystem directories. The first split-out
areas are `common/core`, for the tag execution framework and persistent
STM32-flash state, `common/rtc`, for RTC drivers, `common/storage`, for
external flash drivers, and `common/sensors`, for reusable sensor drivers
grouped by sensor family.

When selecting a module is the same as enabling a firmware capability, the
module fragment defines that capability through `UDEFS`. For example,
`rtc_rv3028` defines `TAG_RTC_RV3028`, while `sensor_accel_adxl362` defines
`TAG_SENSOR_ACCEL_ADXL362`. Keep those module-selection switches out of
`inc/custom.h`; reserve `custom.h` for tag identity, constants, bus choices,
pin aliases, and behavior that is not equivalent to adding a module.

Optional firmware diagnostics follow the same rule. Select the `debug_log`
module to enable the in-firmware debug-message buffer and monitor `Req_debug`
support; do not enable debug logging by defining `DEBUG_MESSAGES` in
`custom.h`.

## Diagnostic Architecture

The tag diagnostic code has two separate pieces:

- `debug_log`: an optional in-firmware text buffer that the monitor can drain.
- `tag_test`: the shared self-test command entry point used by active tags.

### Debug Logging

`debug_log` is intentionally a build module, not a `custom.h` define. A tag that
wants monitor-readable diagnostic text lists the module in `TAG_MODULES`:

```make
TAG_MODULES += \
       tag_core \
       debug_log
```

The module compiles `common/core/src/debug_log.c`, adds the ChibiOS stream
helpers, and defines `TAG_DEBUG_LOG` plus the legacy compatibility name
`DEBUG_MESSAGES`. New code should test `TAG_DEBUG_LOG` or, preferably, include
`debug_log.h` and call the public helpers without adding local preprocessor
guards.

The runtime flow is:

1. `common/core/src/handlers.c` calls `debug_log_init()` when the monitor
   handling thread starts.
2. Driver or runtime code writes optional messages with `debug_log_printf()`.
3. `common/core/src/monitor.c` reports `Ack.status.debug_available`.
4. A host `Req_debug` request drains one `Ack.debug_message` payload with
   `debug_log_read()`.

When the module is absent, `debug_log.h` provides no-op inline helpers. That
lets shared drivers keep low-cost diagnostic calls in place while tags that do
not select `debug_log` pay no buffer or stream cost. The implementation also
guards against the early-boot case: reads and writes return `0` until
`debug_log_init()` has initialized the backing memory stream.

Use debug logging for short diagnostics that may help explain hardware
bring-up, intermittent device failures, or self-test results. It is not a
general event log; the buffer is sized to one protobuf debug-message payload
and is drained by the monitor protocol.

### Self Tests

`tag_test` provides the shared monitor-facing diagnostic entry point. Active
targets should list it in `TAG_MODULES` when they want monitor self-test
support:

```make
TAG_MODULES += \
       tag_core \
       tag_test
```

The shared driver lives in `common/test/src/test.c`. It does not know how to
exercise each device directly. Instead, it maps monitor `TestReq` values onto
small hook functions declared in `common/core/inc/test_support.h` and provided
by the module, tag family, or tag-local code that owns the hardware.

Current hook examples:

- `tag_test_adxl362()` lives with the ADXL362 accelerometer module.
- `tag_test_rtc()` lives with the RV-3028 RTC module.
- `tag_test_external_flash()` lives with the external-flash support code.
- `tag_test_lis2du12()` lives with the common LIS2DU12 accelerometer module.
- `tag_test_lps22hh()` and `tag_test_ak09940a()` live in `IMUTagBreakout/src`,
  because that target currently owns those local driver variants.

The compile-time `TAG_*` switches decide which hooks are compiled into the
shared driver. If a tag selects `sensor_accel_adxl362`, for example, the module
defines `TAG_SENSOR_ACCEL_ADXL362`, compiles `adxl362_test.c`, and the shared
driver calls `tag_test_adxl362()` when the monitor requests the ADXL362 test or
`RUN_ALL`.

Some monitor request names still reflect older hardware:

- `RUN_AIS2` is currently mapped to the LIS2DU12 hook.
- `RUN_MMC5633` is currently mapped to the AK09940A hook.
- `RUN_LPS` is shared by the pressure-sensor hooks.

Keep those protocol names stable until the protobuf monitor interface is
changed. The hook names should still describe the actual device being tested.

When adding a device self-test:

1. Put the test source beside the driver that owns the device, in the relevant
   module, family, or tag-local `src` directory.
2. Name the hook for the actual device, for example `tag_test_newsensor()`.
3. Declare the hook in `test_support.h` under the same `TAG_*` guard used by
   the module or family manifest.
4. Add the hook source basename to the owning module or family manifest.
5. Add the request-to-hook mapping in `common/test/src/test.c`.
6. Update `CUSTOM_DEFINES.md` and rebuild the active tag targets.

Avoid adding a tag-local `src/test.c` unless the entire diagnostic entry point
really must be replaced. Most tags should share `common/test/src/test.c` and
only provide device-specific hooks.

BitTag predates the current shared runtime shape. Its `bt_*.c` files are kept
in `BitTag/src` because they preserve that older firmware behavior and are not
shared by the active tag targets. Its local `src/monitor.c` and
`src/persistent.c` override the generic implementations supplied by `tag_core`.
Legacy inactive targets that once reused those files should be updated to the
common core before being revived.

Tag-specific build constants live in each target's `inc/custom.h`; module-owned
feature switches come from `TAG_MODULES`. See
[`CUSTOM_DEFINES.md`](CUSTOM_DEFINES.md) for the current inventory and the code
paths affected by each define.

## Tag Families

Some hardware comes up first as a breakout and later as a production tag.  Those
variants often need separate board files and `custom.h` values, but most of the
application code should remain identical.  Put that shared application code in a
family directory under `embedded/tags/families`.

For example, `CompassTag`, `CompassTagAT25`, and `CompassTagAT25Breakout`
include:

```make
include ../families/CompassTag/family.mk
```

The inactive `BitTagNG` variants follow the same pattern with:

```make
include ../families/BitTagNG/family.mk
```

The family manifest adds shared include/source directories and lists the shared
source basenames.  The target directories still own board selection, ChibiOS
configuration, `custom.h`, storage-module selection, and any intentionally
divergent sources.

## Template Tag Directory

A tag target should generally have this shape:

```text
embedded/tags/NewTag/
  CMakeLists.txt      CMake target and board/proto dependencies
  Makefile            Usually just: include ../common/make.mk
  project.mk          Board include, TAG_MODULES, and tag-local source list
  cfg/                ChibiOS chconf.h, halconf.h, mcuconf.h
  inc/                tag-local headers and overrides
    custom.h          tag identity, constants, bus choices, and pin aliases
    config.h          stored configuration type and tag type
    datalog.h         log record layout, if the tag logs data
  src/                tag-local implementation and overrides
    config.c
    datalog.c
    pwr.c            optional override for common/core/src/pwr.c
    state_run.c
```

The minimal `CMakeLists.txt` names the firmware target, selects the nanopb
protocol target, and declares the generated board dependency:

```cmake
add_embedded_target(NewTag newtag_proto)
add_dependencies(NewTag board-newtag)
```

The `Makefile` normally delegates to the shared ChibiOS scaffold:

```make
include ../common/make.mk
```

The `project.mk` is the target's build manifest. It pulls in the generated
board make fragment, declares shared modules, then lists tag-local sources:

```make
USE_HAL_I2C_FALLBACK = yes
include $(BOARDDIR)/NewTagBoard/board.mk

TAG_MODULES += \
       protocol_nanopb \
       tag_core \
       tag_test \
       rtc_rv3028 \
       flash_at25xe \
       sensor_pressure_lps27

include ../common/modules/modules.mk
include ../families/NewTagFamily/family.mk  # only when this target is a variant

ALLCSRC += \
       datalog.c \
       state_run.c
```

`common/make.mk` supplies the ChibiOS build plumbing. It includes ChibiOS
startup, HAL, OSAL/RTOS, platform, and rules makefiles, adds family, module,
generated nanopb, and board include paths, and points the build at the
repository `ChibiOS` submodule through the `CHIBIOS` variable passed by CMake.

Board code is generated under the CMake build tree by `embedded/boards` and
included through the board fragment selected in `project.mk`:

```make
include $(BOARDDIR)/NewTagBoard/board.mk
```

Protocol code is generated under the CMake build tree by `embedded/proto-c`.
The `protocol_nanopb` module lists the generated source basenames:

```text
default_config.c
pb_common.c
pb_decode.c
pb_encode.c
tag.pb.c
tagdata.pb.c
```

## Local Overrides

`tag_core` contributes default runtime sources such as `main.c`, `monitor.c`,
`persistent.c`, `pwr.c`, `bus_power.c`, and the state-machine framework. If a
target provides a same-named source in its local `src` directory, the repaired
VPATH order makes that local file override the common default. Local override
files should not be listed again in `ALLCSRC`; the module source basename is
enough.

`bus_power.c` is intentionally separate from `pwr.c`. It provides generic
SPI/I2C descriptor helpers, the default SPI1 controller setup, and standby
pullup/pulldown helpers that operate directly on board-defined `LINE_xxx`
names. Tag-local or module-local power code should describe a device's pins and
off/sleep policy with those helpers instead of duplicating SPI register setup
and STM32 standby-pull register writes.

The shared tag makefile uses this search order:

```make
INCDIR = $(CONFDIR) ./inc $(TAG_FAMILY_INC_DIRS) $(MODULE_INC_DIRS) ../common/inc $(ALLINC) $(TESTINC)
VPATH := $(BUILDDIR) ./src $(TAG_FAMILY_SRC_DIRS) $(MODULE_SRC_DIRS) ../common/src $(PROTODIR) $(NANOPBDIR) $(VPATH)
```

That means tag-local files can override module or shared defaults by using the
same basename:

- `./inc/config.h` is found before `../common/inc/config.h`.
- `./inc/custom.h` is found before any shared `custom.h`.
- `./inc/sensors.h` is found before a family `sensors.h`.
- `./inc/rv3028.h` would be found before
  `../common/rtc/inc/rv3028.h`.
- `./src/config.c` is found before a family `config.c`.
- `./src/handlers.c` would be found before
  `../common/core/src/handlers.c`.
- `./src/monitor.c` would be found before
  `../common/core/src/monitor.c`.
- `./src/rtc_rv3028.c` would be found before
  `../common/rtc/src/rtc_rv3028.c`.
- `../common/rtc/src/hal_rtc_lld.c` is found before the ChibiOS HAL RTC
  implementation because the RTC module adds that directory to `VPATH`.
- `./src/state_machine.c` is found before `../common/core/src/state_machine.c`.

This ordering is deliberate. ChibiOS `rules.mk` normally sorts include paths and
source search paths while building its internal variables. `common/make.mk`
therefore recomputes `IINCDIR` and restores `VPATH` after including ChibiOS
rules so tag-local `inc` and `src` files can override shared defaults.

The module fragments intentionally list source basenames rather than full paths
so this override behavior still works. Prefer avoiding new same-name overrides
when practical; add a clearly named tag-local file instead if the behavior is
not truly an override of a shared default.

The active tag targets use the shared `tag_test` driver in
`common/test/src/test.c`. Device modules, tag families, or tag-local drivers
provide named self-test hooks such as `tag_test_adxl362()` or
`tag_test_lis2du12()`; do not add a new local `src/test.c` unless the target
truly needs to replace the shared diagnostic entry point.

## Adding Shared Code

- Add source files to the narrowest appropriate module in `common/modules`.
- Create a new module when a source group has a distinct owner.
- Keep tag-specific runtime behavior in the tag directory until at least two
  targets truly share it.
- Update `BUILD_SOURCES.md` after changing build membership and rebuilding the
  active targets.
