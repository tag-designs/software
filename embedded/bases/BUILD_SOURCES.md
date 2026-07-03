# Embedded Base Build Sources

This file lists repo-local `.c` and `.h` files used by embedded base targets
that build successfully in the current CMake build. The lists are extracted
from compiler dependency files, then filtered to include only files under
`embedded/bases`.

Generated build files, generated board files, ChibiOS files, toolchain
headers, and other external files are omitted. Local source overrides under
`embedded/bases` are included.

The active embedded base targets in `embedded/bases/CMakeLists.txt` were built
from `/Users/geobrown/Build/tag-designs/software-embedded-submodule`.
The `tag-breakout-base-l432-u375-1v8` target was added and verified from
`/Users/geobrown/Build/tag-designs/software-embedded-clean`.

All active embedded base targets now build successfully in that configuration.

## bittag-base-jlcpcb-v3

### C Files

```text
embedded/bases/bittag-base-jlcpcb-v3/src/main.c
embedded/bases/common/src/ll_swd.c
embedded/bases/common/src/stlink.c
embedded/bases/common/src/stm32adc.c
embedded/bases/common/src/usbcfg.c
```

### Header Files

```text
embedded/bases/bittag-base-jlcpcb-v3/cfg/chconf.h
embedded/bases/bittag-base-jlcpcb-v3/cfg/halconf.h
embedded/bases/bittag-base-jlcpcb-v3/cfg/mcuconf.h
embedded/bases/common/inc/app.h
embedded/bases/common/inc/debug_cm.h
embedded/bases/common/inc/dp_swd.h
embedded/bases/common/inc/stlink.h
embedded/bases/common/inc/stm32f0xx_ll_crs.h
embedded/bases/common/inc/usbcfg.h
```

## bittag-base-v7

### C Files

```text
embedded/bases/bittag-base-v7/src/main.c
embedded/bases/common/src/ll_swd.c
embedded/bases/common/src/stlink.c
embedded/bases/common/src/stm32adc.c
embedded/bases/common/src/usbcfg.c
```

### Header Files

```text
embedded/bases/bittag-base-v7/cfg/chconf.h
embedded/bases/bittag-base-v7/cfg/halconf.h
embedded/bases/bittag-base-v7/cfg/mcuconf.h
embedded/bases/common/inc/app.h
embedded/bases/common/inc/debug_cm.h
embedded/bases/common/inc/dp_swd.h
embedded/bases/common/inc/stlink.h
embedded/bases/common/inc/usbcfg.h
```

## tag-breakout-base-jlcpcb32-v1

### C Files

```text
embedded/bases/common/src/ll_swd.c
embedded/bases/common/src/stlink.c
embedded/bases/common/src/stm32adc.c
embedded/bases/common/src/usbcfg.c
embedded/bases/tag-breakout-base-jlcpcb32-v1/src/main.c
```

### Header Files

```text
embedded/bases/common/inc/app.h
embedded/bases/common/inc/debug_cm.h
embedded/bases/common/inc/dp_swd.h
embedded/bases/common/inc/stlink.h
embedded/bases/common/inc/stm32f0xx_ll_crs.h
embedded/bases/common/inc/usbcfg.h
embedded/bases/tag-breakout-base-jlcpcb32-v1/cfg/chconf.h
embedded/bases/tag-breakout-base-jlcpcb32-v1/cfg/halconf.h
embedded/bases/tag-breakout-base-jlcpcb32-v1/cfg/mcuconf.h
```

## tag-breakout-base-l432-v1

### C Files

```text
embedded/bases/common/src/stlink.c
embedded/bases/common/src/stm32adc.c
embedded/bases/common/src/usbcfg.c
embedded/bases/tag-breakout-base-l432-v1/src/ll_swd_spi.c
embedded/bases/tag-breakout-base-l432-v1/src/main.c
```

### Header Files

```text
embedded/bases/common/inc/app.h
embedded/bases/common/inc/debug_cm.h
embedded/bases/common/inc/dp_swd.h
embedded/bases/common/inc/stlink.h
embedded/bases/common/inc/usbcfg.h
embedded/bases/tag-breakout-base-l432-v1/cfg/chconf.h
embedded/bases/tag-breakout-base-l432-v1/cfg/halconf.h
embedded/bases/tag-breakout-base-l432-v1/cfg/mcuconf.h
```

## tag-breakout-base-l432-u375-1v8

### C Files

```text
embedded/bases/common/src/stlink.c
embedded/bases/tag-breakout-base-l432-u375-1v8/src/ll_swd.c
embedded/bases/tag-breakout-base-l432-u375-1v8/src/main.c
embedded/bases/tag-breakout-base-l432-u375-1v8/src/usbcfg.c
```

### Header Files

```text
embedded/bases/common/inc/app.h
embedded/bases/common/inc/debug_cm.h
embedded/bases/common/inc/dp_swd.h
embedded/bases/common/inc/stlink.h
embedded/bases/common/inc/usbcfg.h
embedded/bases/tag-breakout-base-l432-u375-1v8/cfg/chconf.h
embedded/bases/tag-breakout-base-l432-u375-1v8/cfg/halconf.h
embedded/bases/tag-breakout-base-l432-u375-1v8/cfg/mcuconf.h
```

## tag-base-c071

### C Files

```text
embedded/bases/common/src/stlink.c
embedded/bases/common/src/stm32adc.c
embedded/bases/common/src/usbcfg.c
embedded/bases/tag-base-c071/src/ll_swd_spi.c
embedded/bases/tag-base-c071/src/main.c
```

### Header Files

```text
embedded/bases/common/inc/app.h
embedded/bases/common/inc/debug_cm.h
embedded/bases/common/inc/dp_swd.h
embedded/bases/common/inc/stlink.h
embedded/bases/common/inc/usbcfg.h
embedded/bases/tag-base-c071/cfg/chconf.h
embedded/bases/tag-base-c071/cfg/halconf.h
embedded/bases/tag-base-c071/cfg/mcuconf.h
```
