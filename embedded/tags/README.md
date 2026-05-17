# Embedded Tag Firmware

Tag firmware targets share a ChibiOS makefile scaffold from `common/make.mk`.
Each target's `project.mk` now separates two concerns:

1. `TAG_MODULES`: shared source groups from `common/modules`.
2. `ALLCSRC`: tag-local application files from that target's `src` directory.

Example:

```make
TAG_MODULES += \
       protocol_nanopb \
       tag_core \
       rtc_rv3028 \
       flash_at25xe

include ../common/modules/modules.mk

ALLCSRC += \
       config.c \
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
`rtc_rv3028` defines `TAG_RTC_RV3028` and compatibility switch `RV3028_RTC`,
while `sensor_accel_adxl362` defines `TAG_SENSOR_ACCEL_ADXL362` and
compatibility switch `USE_ADXL362`. Keep those module-selection switches out of
`inc/custom.h`; reserve `custom.h` for tag identity, constants, bus choices,
pin aliases, and behavior that is not equivalent to adding a module.

BitTag predates the current shared runtime shape. Its `bt_*.c` files are kept
in `BitTag/src` because they preserve that older firmware behavior and are not
shared by the active tag targets. Its local `src/monitor.c` and
`src/persistent.c` override the generic implementations supplied by `tag_core`.
Legacy inactive targets that once reused those files should be updated to the
common core before being revived.

Build-time feature switches live in each target's `inc/custom.h`. See
[`CUSTOM_DEFINES.md`](CUSTOM_DEFINES.md) for the current inventory and the code
paths affected by each define.

## Template Tag Directory

A tag target should generally have this shape:

```text
embedded/tags/NewTag/
  CMakeLists.txt      CMake target and board/proto dependencies
  Makefile            Usually just: include ../common/make.mk
  project.mk          Board include, TAG_MODULES, and tag-local source list
  cfg/                ChibiOS chconf.h, halconf.h, mcuconf.h
  inc/                tag-local headers and overrides
    custom.h          build/profile feature defines for this firmware image
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

ALLCSRC += \
       config.c \
       datalog.c \
       state_run.c
```

`common/make.mk` supplies the ChibiOS build plumbing. It includes ChibiOS
startup, HAL, OSAL/RTOS, platform, and rules makefiles, adds generated nanopb
and board include paths, and points the build at the repository `ChibiOS`
submodule through the `CHIBIOS` variable passed by CMake.

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
`persistent.c`, `pwr.c`, and the state-machine framework. If a target provides
a same-named source in its local `src` directory, the repaired VPATH order makes
that local file override the common default. Local override files should not be
listed again in `ALLCSRC`; the module source basename is enough.

The shared tag makefile uses this search order:

```make
INCDIR = $(CONFDIR) ./inc $(MODULE_INC_DIRS) ../common/inc $(ALLINC) $(TESTINC)
VPATH := $(BUILDDIR) ./src $(MODULE_SRC_DIRS) ../common/src $(PROTODIR) $(NANOPBDIR) $(VPATH)
```

That means tag-local files can override module or shared defaults by using the
same basename:

- `./inc/config.h` is found before `../common/inc/config.h`.
- `./inc/custom.h` is found before any shared `custom.h`.
- `./inc/rv3028.h` would be found before
  `../common/rtc/inc/rv3028.h`.
- `./src/test.c` is found before `../common/test/src/test.c`.
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
so this override behavior still works. For example, `tag_test.mk` adds
`test.c`; a tag with `src/test.c` gets its local test file, while a tag without
one gets `common/test/src/test.c`.

A few targets rely on this while being split out, notably local `test.c` and,
for some development targets, local state-machine code. Prefer avoiding new
same-name overrides when practical; add a clearly named tag-local file instead
if the behavior is not truly an override of a shared default.

## Adding Shared Code

- Add source files to the narrowest appropriate module in `common/modules`.
- Create a new module when a source group has a distinct owner.
- Keep tag-specific runtime behavior in the tag directory until at least two
  targets truly share it.
- Update `BUILD_SOURCES.md` after changing build membership and rebuilding the
  active targets.
