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
  inc/app.h
  src/handlers.c
  src/main.c
  src/monitor.c
  src/state_machine.c
  src/stm32adc.c
  src/stm32flash.c
  src/time.c

../rtc/
  inc/rv3028.h
  src/hal_rtc_lld.c
  src/rtc_rv3028.c
```

`hal_rtc_lld.c` is a special case: ChibiOS adds that source basename itself,
and the RTC module supplies the local source directory so the tag build finds
the repo-local override before the ChibiOS HAL implementation.

When adding a shared source file:

- add it to the narrowest module that owns the behavior;
- create a new module if no existing group fits cleanly;
- add module-owned source/include directories through `MODULE_SRC_DIRS` and
  `MODULE_INC_DIRS` when the files live under the module directory;
- keep tag-local application code listed directly in that tag's `project.mk`.
