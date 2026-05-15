# Embedded Tag Build Sources

This file lists repo-local `.c` and `.h` files used by embedded tag targets
that build successfully in the current CMake build. The lists are extracted
from compiler dependency files, then filtered to include only files under
`embedded/tags`.

Generated build files, generated nanopb files, generated board files, ChibiOS
files, nanopb runtime files, toolchain headers, and other external files are
omitted. Local source overrides under `embedded/tags` are included.

Skipped active CMake tag targets:

| Target | Reason |
| --- | --- |
| `BitPresTagMX25R` | Fails in board/ChibiOS configuration with `obsolete or unknown configuration file`. |
| `PresTagBreakout` | Fails in board/ChibiOS configuration with `obsolete or unknown configuration file`. |
| `PresTagBMPBreakout` | Fails in board/ChibiOS configuration with `obsolete or unknown configuration file`. |
| `BreakoutDownloadTest` | Fails in board/ChibiOS configuration with `obsolete or unknown configuration file`. |
| `BitTagNG` | Fails in board/ChibiOS configuration with `obsolete or unknown configuration file`. |
| `BitTagNG-lis2du12` | Fails in board/ChibiOS configuration with `obsolete or unknown configuration file`. |
| `IMUTagBreakout` | Fails compiling `mx25l.c` due to an implicit `chprintf` declaration. |

## BitTag

### C Files

```text
embedded/tags/common/src/ADXL362.c
embedded/tags/common/src/bt_config.c
embedded/tags/common/src/bt_monitor.c
embedded/tags/common/src/bt_persistent.c
embedded/tags/common/src/bt_pwr.c
embedded/tags/common/src/bt_state_run.c
embedded/tags/common/src/hal_rtc_lld.c
embedded/tags/common/src/handlers.c
embedded/tags/common/src/main.c
embedded/tags/common/src/rtc_rv3028.c
embedded/tags/common/src/state_machine.c
embedded/tags/common/src/stm32adc.c
embedded/tags/common/src/stm32flash.c
embedded/tags/common/src/test.c
embedded/tags/common/src/time.c
embedded/tags/BitTag/src/hal_lld.c
```

### Header Files

```text
embedded/tags/common/inc/ADXL362.h
embedded/tags/common/inc/ais2dw12.h
embedded/tags/common/inc/app.h
embedded/tags/common/inc/config.h
embedded/tags/common/inc/external_flash.h
embedded/tags/common/inc/lis2dtw12.h
embedded/tags/common/inc/lps.h
embedded/tags/common/inc/mmc5633.h
embedded/tags/common/inc/opt3002.h
embedded/tags/common/inc/persistent.h
embedded/tags/common/inc/rv3028.h
embedded/tags/BitTag/Inc/custom.h
embedded/tags/BitTag/cfg/chconf.h
embedded/tags/BitTag/cfg/halconf.h
embedded/tags/BitTag/cfg/mcuconf.h
```

## PresTag

### C Files

```text
embedded/tags/common/src/at25xe.c
embedded/tags/common/src/hal_rtc_lld.c
embedded/tags/common/src/handlers.c
embedded/tags/common/src/lps27.c
embedded/tags/common/src/main.c
embedded/tags/common/src/monitor.c
embedded/tags/common/src/persistent.c
embedded/tags/common/src/rtc_rv3028.c
embedded/tags/common/src/state_machine.c
embedded/tags/common/src/stm32adc.c
embedded/tags/common/src/stm32flash.c
embedded/tags/common/src/test.c
embedded/tags/common/src/time.c
embedded/tags/PresTag/src/config.c
embedded/tags/PresTag/src/datalog.c
embedded/tags/PresTag/src/pwr.c
embedded/tags/PresTag/src/state_run.c
```

### Header Files

```text
embedded/tags/common/inc/ais2dw12.h
embedded/tags/common/inc/app.h
embedded/tags/common/inc/external_flash.h
embedded/tags/common/inc/lis2dtw12.h
embedded/tags/common/inc/lps.h
embedded/tags/common/inc/lps27hhw.h
embedded/tags/common/inc/mmc5633.h
embedded/tags/common/inc/opt3002.h
embedded/tags/common/inc/rv3028.h
embedded/tags/PresTag/Inc/config.h
embedded/tags/PresTag/Inc/custom.h
embedded/tags/PresTag/Inc/datalog.h
embedded/tags/PresTag/Inc/persistent.h
embedded/tags/PresTag/cfg/chconf.h
embedded/tags/PresTag/cfg/halconf.h
embedded/tags/PresTag/cfg/mcuconf.h
```

## BitPresTag

### C Files

```text
embedded/tags/common/src/ADXL362.c
embedded/tags/common/src/at25xe.c
embedded/tags/common/src/hal_rtc_lld.c
embedded/tags/common/src/handlers.c
embedded/tags/common/src/lps27.c
embedded/tags/common/src/main.c
embedded/tags/common/src/monitor.c
embedded/tags/common/src/persistent.c
embedded/tags/common/src/rtc_rv3028.c
embedded/tags/common/src/state_machine.c
embedded/tags/common/src/stm32adc.c
embedded/tags/common/src/stm32flash.c
embedded/tags/common/src/time.c
embedded/tags/BitPresTag/src/config.c
embedded/tags/BitPresTag/src/datalog.c
embedded/tags/BitPresTag/src/pwr.c
embedded/tags/BitPresTag/src/state_run.c
embedded/tags/BitPresTag/src/test.c
```

### Header Files

```text
embedded/tags/common/inc/ADXL362.h
embedded/tags/common/inc/ais2dw12.h
embedded/tags/common/inc/app.h
embedded/tags/common/inc/external_flash.h
embedded/tags/common/inc/lis2dtw12.h
embedded/tags/common/inc/lps.h
embedded/tags/common/inc/lps27hhw.h
embedded/tags/common/inc/mmc5633.h
embedded/tags/common/inc/opt3002.h
embedded/tags/common/inc/rv3028.h
embedded/tags/BitPresTag/Inc/config.h
embedded/tags/BitPresTag/Inc/custom.h
embedded/tags/BitPresTag/Inc/datalog.h
embedded/tags/BitPresTag/Inc/persistent.h
embedded/tags/BitPresTag/cfg/chconf.h
embedded/tags/BitPresTag/cfg/halconf.h
embedded/tags/BitPresTag/cfg/mcuconf.h
```

## CompassTag

### C Files

```text
embedded/tags/common/src/ak09940a.c
embedded/tags/common/src/hal_rtc_lld.c
embedded/tags/common/src/handlers.c
embedded/tags/common/src/main.c
embedded/tags/common/src/monitor.c
embedded/tags/common/src/mx25r.c
embedded/tags/common/src/persistent.c
embedded/tags/common/src/rtc_rv3028.c
embedded/tags/common/src/state_machine.c
embedded/tags/common/src/stm32adc.c
embedded/tags/common/src/stm32flash.c
embedded/tags/common/src/time.c
embedded/tags/CompassTag/src/config.c
embedded/tags/CompassTag/src/datalog.c
embedded/tags/CompassTag/src/lis2du12.c
embedded/tags/CompassTag/src/pwr.c
embedded/tags/CompassTag/src/sensors.c
embedded/tags/CompassTag/src/state_run.c
embedded/tags/CompassTag/src/test.c
```

### Header Files

```text
embedded/tags/common/inc/ak09940a.h
embedded/tags/common/inc/app.h
embedded/tags/common/inc/external_flash.h
embedded/tags/common/inc/rv3028.h
embedded/tags/CompassTag/Inc/config.h
embedded/tags/CompassTag/Inc/custom.h
embedded/tags/CompassTag/Inc/datalog.h
embedded/tags/CompassTag/Inc/lis2du12.h
embedded/tags/CompassTag/Inc/persistent.h
embedded/tags/CompassTag/Inc/sensors.h
embedded/tags/CompassTag/cfg/chconf.h
embedded/tags/CompassTag/cfg/halconf.h
embedded/tags/CompassTag/cfg/mcuconf.h
```

## CompassTagAT25Breakout

### C Files

```text
embedded/tags/common/src/ak09940a.c
embedded/tags/common/src/at25xe.c
embedded/tags/common/src/hal_rtc_lld.c
embedded/tags/common/src/handlers.c
embedded/tags/common/src/main.c
embedded/tags/common/src/monitor.c
embedded/tags/common/src/persistent.c
embedded/tags/common/src/rtc_rv3028.c
embedded/tags/common/src/state_machine.c
embedded/tags/common/src/stm32adc.c
embedded/tags/common/src/stm32flash.c
embedded/tags/common/src/time.c
embedded/tags/CompassTagAT25Breakout/src/config.c
embedded/tags/CompassTagAT25Breakout/src/datalog.c
embedded/tags/CompassTagAT25Breakout/src/lis2du12.c
embedded/tags/CompassTagAT25Breakout/src/pwr.c
embedded/tags/CompassTagAT25Breakout/src/sensors.c
embedded/tags/CompassTagAT25Breakout/src/state_run.c
embedded/tags/CompassTagAT25Breakout/src/test.c
```

### Header Files

```text
embedded/tags/common/inc/ak09940a.h
embedded/tags/common/inc/app.h
embedded/tags/common/inc/external_flash.h
embedded/tags/common/inc/rv3028.h
embedded/tags/CompassTagAT25Breakout/Inc/config.h
embedded/tags/CompassTagAT25Breakout/Inc/custom.h
embedded/tags/CompassTagAT25Breakout/Inc/datalog.h
embedded/tags/CompassTagAT25Breakout/Inc/lis2du12.h
embedded/tags/CompassTagAT25Breakout/Inc/persistent.h
embedded/tags/CompassTagAT25Breakout/Inc/sensors.h
embedded/tags/CompassTagAT25Breakout/cfg/chconf.h
embedded/tags/CompassTagAT25Breakout/cfg/halconf.h
embedded/tags/CompassTagAT25Breakout/cfg/mcuconf.h
```

## CompassTagAT25

### C Files

```text
embedded/tags/common/src/ak09940a.c
embedded/tags/common/src/at25xe.c
embedded/tags/common/src/hal_rtc_lld.c
embedded/tags/common/src/handlers.c
embedded/tags/common/src/main.c
embedded/tags/common/src/monitor.c
embedded/tags/common/src/persistent.c
embedded/tags/common/src/rtc_rv3028.c
embedded/tags/common/src/state_machine.c
embedded/tags/common/src/stm32adc.c
embedded/tags/common/src/stm32flash.c
embedded/tags/common/src/time.c
embedded/tags/CompassTagAT25/src/config.c
embedded/tags/CompassTagAT25/src/datalog.c
embedded/tags/CompassTagAT25/src/lis2du12.c
embedded/tags/CompassTagAT25/src/pwr.c
embedded/tags/CompassTagAT25/src/sensors.c
embedded/tags/CompassTagAT25/src/state_run.c
embedded/tags/CompassTagAT25/src/test.c
```

### Header Files

```text
embedded/tags/common/inc/ak09940a.h
embedded/tags/common/inc/app.h
embedded/tags/common/inc/external_flash.h
embedded/tags/common/inc/rv3028.h
embedded/tags/CompassTagAT25/Inc/config.h
embedded/tags/CompassTagAT25/Inc/custom.h
embedded/tags/CompassTagAT25/Inc/datalog.h
embedded/tags/CompassTagAT25/Inc/lis2du12.h
embedded/tags/CompassTagAT25/Inc/persistent.h
embedded/tags/CompassTagAT25/Inc/sensors.h
embedded/tags/CompassTagAT25/cfg/chconf.h
embedded/tags/CompassTagAT25/cfg/halconf.h
embedded/tags/CompassTagAT25/cfg/mcuconf.h
```

## CompassTag-debug

### C Files

```text
embedded/tags/common/src/hal_rtc_lld.c
embedded/tags/common/src/rtc_rv3028.c
embedded/tags/common/src/time.c
embedded/tags/CompassTag-debug/src/handlers.c
embedded/tags/CompassTag-debug/src/main.c
```

### Header Files

```text
embedded/tags/common/inc/app.h
embedded/tags/common/inc/rv3028.h
embedded/tags/CompassTag-debug/Inc/custom.h
embedded/tags/CompassTag-debug/Inc/lis2du12.h
embedded/tags/CompassTag-debug/Inc/sensors.h
embedded/tags/CompassTag-debug/cfg/chconf.h
embedded/tags/CompassTag-debug/cfg/halconf.h
embedded/tags/CompassTag-debug/cfg/mcuconf.h
```
