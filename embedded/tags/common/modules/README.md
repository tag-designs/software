# Tag Common Build Modules

The tag firmware build still uses ChibiOS makefiles, but shared sources are now
grouped by module instead of being listed individually in every tag
`project.mk`.

Each tag declares:

```make
TAG_MODULES += \
       protocol_nanopb \
       tag_core \
       rtc_rv3028

include ../common/modules/modules.mk
```

The module fragments in this directory append source basenames to `ALLCSRC`.
They deliberately do not use full paths. A module that owns source or header
files also appends its directories to `MODULE_SRC_DIRS` and `MODULE_INC_DIRS`.
Modules also add their feature switches to `UDEFS` when selecting the module is
equivalent to enabling a compile-time capability. The preferred switches use
`TAG_*` names, while older compatibility switches such as `USE_ADXL362` remain
defined for legacy local code and inactive prototypes.
The ChibiOS makefile's repaired `VPATH` searches the tag-local `src` directory
before module source directories and `../common/src`, so a tag can override a
shared default by providing a same-named local source file. Likewise, tag-local
headers in `inc` are searched before module include directories and
`../common/inc`.

This depends on `../make.mk` repairing the order after ChibiOS `rules.mk` is
included. ChibiOS sorts its generated include flags and source paths; the common
tag makefile rebuilds `IINCDIR` and `VPATH` so local override directories stay
ahead of shared defaults.

Some physical shared sources still live in `../src` and headers in `../inc`.
Subsystems are being moved into real common-code directories as they are cleaned
up. Current examples:

```text
../core/
  inc/adc.h
  inc/app.h
  inc/core_events.h
  inc/core_runtime.h
  inc/core_state.h
  inc/core_sync.h
  inc/core_types.h
  inc/debug_log.h
  inc/flash_internal.h
  inc/gpio_utils.h
  inc/persistent.h
  inc/power.h
  inc/sensor_calibration.h
  inc/test_support.h
  inc/timekeeping.h
  src/handlers.c
  src/main.c
  src/monitor.c
  src/persistent.c
  src/pwr.c
  src/state_machine.c
  src/stm32adc.c
  src/stm32flash.c
  src/time.c

../rtc/
  inc/rtc_api.h
  inc/rv3028.h
  inc/rv3032.h
  inc/rv8803.h
  src/hal_rtc_lld.c
  src/rtc_rv3028.c
  src/rtc_rv3032.c
  src/rtc_rv8803.c

../storage/
  inc/at25xe.h
  inc/external_flash.h
  inc/mx25r.h
  src/at25xe.c
  src/mx25l.c
  src/mx25r.c

../test/
  src/test.c

../sensors/
  accel/
    inc/ADXL362.h
    inc/ADXL367.h
    inc/ais2dw12.h
    inc/lis2dtw12.h
    src/ADXL362.c
    src/ADXL367.c
    src/ais2dw12.c
    src/lis2dtw12.c
  pressure/
    inc/lps.h
    inc/lps27hhw.h
    src/lps27.c
  mag/
    inc/ak09940a.h
    inc/mmc5633.h
    src/ak09940a.c
    src/mmc5633.c
  light/
    inc/opt3002.h
    src/opt3002.c
```

`hal_rtc_lld.c` is a special case: ChibiOS adds that source basename itself,
and the RTC module supplies the local source directory so the tag build finds
the repo-local override before the ChibiOS HAL implementation.

Core owns the persistent state layer because that state is stored in STM32
flash and used by the tag runtime. It also owns the default power-management
hooks in `pwr.c`; current active tags provide local `src/pwr.c` overrides, so
they no longer list `pwr.c` directly in `ALLCSRC`. The old monolithic
`app.h` now acts as a compatibility umbrella over topic-specific headers such
as `adc.h`, `core_events.h`, `power.h`, and `timekeeping.h`. New code should
prefer the narrow header for the subsystem it uses. Shared code under
`common/core/src` should not include `app.h`; that keeps header dependencies
visible and lets `app.h` shrink over time.

Storage owns the external-memory interface and chip drivers. `flash_at25xe`,
`flash_mx25l`, and `flash_mx25r` select the external-memory implementation used
by a particular tag. `storage_paths.mk` is a guarded helper included by those
modules and by `tag_core`; it is not intended to be listed directly in
`TAG_MODULES`.

Sensors are grouped by family rather than by tag type. Production sensor
modules such as `sensor_pressure_lps27`, `sensor_accel_adxl362`, and
`sensor_mag_ak09940a` add only the family paths and source files they compile.
`sensor_paths.mk` is a broader guarded helper for shared diagnostic code that
has compile-time branches for multiple sensor families; it is not intended to
be listed directly in `TAG_MODULES`.

Test owns the shared factory/diagnostic command implementation. `tag_test`
adds `../test/src` to the source search path and lists the basename `test.c`,
so a target can still provide `src/test.c` when its diagnostics are
tag-specific.

When adding a shared source file:

- add it to the narrowest module that owns the behavior;
- define module-owned feature switches through `UDEFS` when the module selects
  hardware or behavior that shared C code tests with preprocessor guards;
- create a new module if no existing group fits cleanly;
- add module-owned source/include directories through `MODULE_SRC_DIRS` and
  `MODULE_INC_DIRS` when the files live under the module directory;
- keep tag-local application code listed directly in that tag's `project.mk`.
