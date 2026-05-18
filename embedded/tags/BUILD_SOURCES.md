# Embedded Tag Build Sources

This file lists repo-local `.c` and `.h` files used by embedded tag targets
that build successfully in the current CMake build. The lists are extracted
from compiler dependency files, then filtered to include only files under
`embedded/tags`.

Generated build files, generated nanopb files, generated board files, ChibiOS
files, nanopb runtime files, toolchain headers, and other external files are
omitted. Local source overrides under `embedded/tags` are included.

The active embedded tag targets in `embedded/tags/CMakeLists.txt` were rebuilt
from `/private/tmp/tag-software-embedded-testrefactor` after regenerating that
build tree.

All active embedded tag targets now build successfully in that configuration.

## BitTag

### C Files

```text
embedded/tags/BitTag/src/hal_lld.c
embedded/tags/BitTag/src/ADXL362.c
embedded/tags/BitTag/src/adxl362_test.c
embedded/tags/BitTag/src/bt_config.c
embedded/tags/BitTag/src/monitor.c
embedded/tags/BitTag/src/persistent.c
embedded/tags/BitTag/src/pwr.c
embedded/tags/BitTag/src/bt_state_run.c
embedded/tags/BitTag/src/hal_rtc_lld.c
embedded/tags/BitTag/src/rtc_rv3028.c
embedded/tags/BitTag/src/rtc_test.c
embedded/tags/common/core/src/bus_power.c
embedded/tags/common/core/src/handlers.c
embedded/tags/common/core/src/i2c_bus.c
embedded/tags/common/core/src/main.c
embedded/tags/common/core/src/spi_bus.c
embedded/tags/common/core/src/state_machine.c
embedded/tags/common/core/src/stm32adc.c
embedded/tags/common/core/src/stm32flash.c
embedded/tags/common/test/src/test.c
embedded/tags/common/core/src/time.c
```

### Header Files

```text
embedded/tags/BitTag/Inc/custom.h
embedded/tags/BitTag/cfg/chconf.h
embedded/tags/BitTag/cfg/halconf.h
embedded/tags/BitTag/cfg/mcuconf.h
embedded/tags/BitTag/inc/ADXL362.h
embedded/tags/BitTag/inc/rtc_api.h
embedded/tags/BitTag/inc/rv3028.h
embedded/tags/common/sensors/accel/inc/ais2dw12.h
embedded/tags/common/core/inc/app.h
embedded/tags/common/core/inc/adc.h
embedded/tags/common/core/inc/core_events.h
embedded/tags/common/core/inc/core_runtime.h
embedded/tags/common/core/inc/core_state.h
embedded/tags/common/core/inc/core_sync.h
embedded/tags/common/core/inc/core_types.h
embedded/tags/common/core/inc/debug_log.h
embedded/tags/common/core/inc/flash_internal.h
embedded/tags/common/core/inc/gpio_utils.h
embedded/tags/common/core/inc/i2c_bus.h
embedded/tags/common/core/inc/power.h
embedded/tags/common/core/inc/sensor_calibration.h
embedded/tags/common/core/inc/spi_bus.h
embedded/tags/common/core/inc/test_support.h
embedded/tags/common/core/inc/timekeeping.h
embedded/tags/common/inc/config.h
embedded/tags/common/storage/inc/external_flash.h
embedded/tags/common/sensors/accel/inc/lis2dtw12.h
embedded/tags/common/sensors/pressure/inc/lps.h
embedded/tags/common/sensors/mag/inc/mmc5633.h
embedded/tags/common/sensors/light/inc/opt3002.h
embedded/tags/common/core/inc/persistent.h
```

## PresTag

### C Files

```text
embedded/tags/PresTag/src/config.c
embedded/tags/PresTag/src/datalog.c
embedded/tags/PresTag/src/pwr.c
embedded/tags/PresTag/src/state_run.c
embedded/tags/common/storage/src/at25xe.c
embedded/tags/common/storage/src/external_flash_test.c
embedded/tags/common/rtc/src/hal_rtc_lld.c
embedded/tags/common/core/src/bus_power.c
embedded/tags/common/core/src/handlers.c
embedded/tags/common/core/src/i2c_bus.c
embedded/tags/common/sensors/src/sensor_io.c
embedded/tags/common/sensors/pressure/src/lps.c
embedded/tags/common/sensors/pressure/src/lps27.c
embedded/tags/common/sensors/pressure/src/lps27_test.c
embedded/tags/common/core/src/main.c
embedded/tags/common/core/src/monitor.c
embedded/tags/common/core/src/persistent.c
embedded/tags/common/rtc/src/rtc_rv3028.c
embedded/tags/common/rtc/src/rtc_test.c
embedded/tags/common/core/src/state_machine.c
embedded/tags/common/core/src/stm32adc.c
embedded/tags/common/core/src/stm32flash.c
embedded/tags/common/test/src/test.c
embedded/tags/common/core/src/time.c
```

### Header Files

```text
embedded/tags/PresTag/Inc/config.h
embedded/tags/PresTag/Inc/custom.h
embedded/tags/PresTag/Inc/datalog.h
embedded/tags/PresTag/Inc/persistent.h
embedded/tags/PresTag/cfg/chconf.h
embedded/tags/PresTag/cfg/halconf.h
embedded/tags/PresTag/cfg/mcuconf.h
embedded/tags/common/sensors/accel/inc/ais2dw12.h
embedded/tags/common/core/inc/app.h
embedded/tags/common/core/inc/adc.h
embedded/tags/common/core/inc/core_events.h
embedded/tags/common/core/inc/core_runtime.h
embedded/tags/common/core/inc/core_state.h
embedded/tags/common/core/inc/core_sync.h
embedded/tags/common/core/inc/core_types.h
embedded/tags/common/core/inc/debug_log.h
embedded/tags/common/core/inc/flash_internal.h
embedded/tags/common/core/inc/gpio_utils.h
embedded/tags/common/core/inc/i2c_bus.h
embedded/tags/common/core/inc/power.h
embedded/tags/common/core/inc/sensor_calibration.h
embedded/tags/common/core/inc/test_support.h
embedded/tags/common/core/inc/timekeeping.h
embedded/tags/common/rtc/inc/rtc_api.h
embedded/tags/common/storage/inc/external_flash.h
embedded/tags/common/sensors/accel/inc/lis2dtw12.h
embedded/tags/common/sensors/inc/sensor_io.h
embedded/tags/common/sensors/pressure/inc/lps.h
embedded/tags/common/sensors/pressure/inc/lps27hhw.h
embedded/tags/common/sensors/mag/inc/mmc5633.h
embedded/tags/common/sensors/light/inc/opt3002.h
embedded/tags/common/rtc/inc/rv3028.h
```

## BitPresTag

### C Files

```text
embedded/tags/BitPresTag/src/config.c
embedded/tags/BitPresTag/src/datalog.c
embedded/tags/BitPresTag/src/pwr.c
embedded/tags/BitPresTag/src/state_run.c
embedded/tags/common/sensors/accel/src/ADXL362.c
embedded/tags/common/sensors/accel/src/adxl362_test.c
embedded/tags/common/storage/src/at25xe.c
embedded/tags/common/storage/src/external_flash_test.c
embedded/tags/common/rtc/src/hal_rtc_lld.c
embedded/tags/common/core/src/bus_power.c
embedded/tags/common/core/src/handlers.c
embedded/tags/common/core/src/i2c_bus.c
embedded/tags/common/sensors/src/sensor_io.c
embedded/tags/common/sensors/pressure/src/lps.c
embedded/tags/common/sensors/pressure/src/lps27.c
embedded/tags/common/sensors/pressure/src/lps27_test.c
embedded/tags/common/core/src/main.c
embedded/tags/common/core/src/monitor.c
embedded/tags/common/core/src/persistent.c
embedded/tags/common/rtc/src/rtc_rv3028.c
embedded/tags/common/rtc/src/rtc_test.c
embedded/tags/common/core/src/state_machine.c
embedded/tags/common/core/src/stm32adc.c
embedded/tags/common/core/src/stm32flash.c
embedded/tags/common/test/src/test.c
embedded/tags/common/core/src/time.c
```

### Header Files

```text
embedded/tags/BitPresTag/Inc/config.h
embedded/tags/BitPresTag/Inc/custom.h
embedded/tags/BitPresTag/Inc/datalog.h
embedded/tags/BitPresTag/Inc/persistent.h
embedded/tags/BitPresTag/cfg/chconf.h
embedded/tags/BitPresTag/cfg/halconf.h
embedded/tags/BitPresTag/cfg/mcuconf.h
embedded/tags/common/sensors/accel/inc/ADXL362.h
embedded/tags/common/sensors/accel/inc/ais2dw12.h
embedded/tags/common/core/inc/app.h
embedded/tags/common/core/inc/adc.h
embedded/tags/common/core/inc/core_events.h
embedded/tags/common/core/inc/core_runtime.h
embedded/tags/common/core/inc/core_state.h
embedded/tags/common/core/inc/core_sync.h
embedded/tags/common/core/inc/core_types.h
embedded/tags/common/core/inc/debug_log.h
embedded/tags/common/core/inc/flash_internal.h
embedded/tags/common/core/inc/gpio_utils.h
embedded/tags/common/core/inc/i2c_bus.h
embedded/tags/common/core/inc/power.h
embedded/tags/common/core/inc/sensor_calibration.h
embedded/tags/common/core/inc/test_support.h
embedded/tags/common/core/inc/timekeeping.h
embedded/tags/common/rtc/inc/rtc_api.h
embedded/tags/common/storage/inc/external_flash.h
embedded/tags/common/sensors/accel/inc/lis2dtw12.h
embedded/tags/common/sensors/inc/sensor_io.h
embedded/tags/common/sensors/pressure/inc/lps.h
embedded/tags/common/sensors/pressure/inc/lps27hhw.h
embedded/tags/common/sensors/mag/inc/mmc5633.h
embedded/tags/common/sensors/light/inc/opt3002.h
embedded/tags/common/rtc/inc/rv3028.h
```

## CompassTag

### C Files

```text
embedded/tags/CompassTag/src/config.c
embedded/tags/CompassTag/src/datalog.c
embedded/tags/CompassTag/src/lis2du12.c
embedded/tags/families/CompassTag/src/pwr.c
embedded/tags/families/CompassTag/src/sensors.c
embedded/tags/CompassTag/src/state_run.c
embedded/tags/common/test/src/test.c
embedded/tags/common/sensors/src/sensor_io.c
embedded/tags/common/sensors/mag/src/ak09940a.c
embedded/tags/common/sensors/mag/src/ak09940a_shim.c
embedded/tags/common/sensors/mag/src/ak09940a_test.c
embedded/tags/families/CompassTag/src/lis2du12_test.c
embedded/tags/common/rtc/src/hal_rtc_lld.c
embedded/tags/common/core/src/bus_power.c
embedded/tags/common/core/src/handlers.c
embedded/tags/common/core/src/i2c_bus.c
embedded/tags/common/core/src/main.c
embedded/tags/common/core/src/monitor.c
embedded/tags/common/storage/src/mx25r.c
embedded/tags/common/storage/src/external_flash_test.c
embedded/tags/common/core/src/persistent.c
embedded/tags/common/rtc/src/rtc_rv3028.c
embedded/tags/common/rtc/src/rtc_test.c
embedded/tags/common/core/src/state_machine.c
embedded/tags/common/core/src/stm32adc.c
embedded/tags/common/core/src/stm32flash.c
embedded/tags/common/core/src/time.c
```

### Header Files

```text
embedded/tags/CompassTag/Inc/config.h
embedded/tags/CompassTag/Inc/custom.h
embedded/tags/CompassTag/Inc/datalog.h
embedded/tags/CompassTag/Inc/lis2du12.h
embedded/tags/CompassTag/Inc/persistent.h
embedded/tags/CompassTag/Inc/sensors.h
embedded/tags/CompassTag/cfg/chconf.h
embedded/tags/CompassTag/cfg/halconf.h
embedded/tags/CompassTag/cfg/mcuconf.h
embedded/tags/common/sensors/mag/inc/ak09940a.h
embedded/tags/common/core/inc/app.h
embedded/tags/common/core/inc/adc.h
embedded/tags/common/core/inc/core_events.h
embedded/tags/common/core/inc/core_runtime.h
embedded/tags/common/core/inc/core_state.h
embedded/tags/common/core/inc/core_sync.h
embedded/tags/common/core/inc/core_types.h
embedded/tags/common/core/inc/debug_log.h
embedded/tags/common/core/inc/flash_internal.h
embedded/tags/common/core/inc/gpio_utils.h
embedded/tags/common/core/inc/i2c_bus.h
embedded/tags/common/core/inc/power.h
embedded/tags/common/core/inc/sensor_calibration.h
embedded/tags/common/core/inc/test_support.h
embedded/tags/common/core/inc/timekeeping.h
embedded/tags/common/rtc/inc/rtc_api.h
embedded/tags/common/storage/inc/external_flash.h
embedded/tags/common/rtc/inc/rv3028.h
```

## CompassTagAT25Breakout

### C Files

```text
embedded/tags/CompassTagAT25Breakout/src/config.c
embedded/tags/CompassTagAT25Breakout/src/datalog.c
embedded/tags/CompassTagAT25Breakout/src/lis2du12.c
embedded/tags/families/CompassTag/src/pwr.c
embedded/tags/families/CompassTag/src/sensors.c
embedded/tags/CompassTagAT25Breakout/src/state_run.c
embedded/tags/common/test/src/test.c
embedded/tags/common/sensors/src/sensor_io.c
embedded/tags/common/sensors/mag/src/ak09940a.c
embedded/tags/common/sensors/mag/src/ak09940a_shim.c
embedded/tags/common/sensors/mag/src/ak09940a_test.c
embedded/tags/families/CompassTag/src/lis2du12_test.c
embedded/tags/common/storage/src/at25xe.c
embedded/tags/common/storage/src/external_flash_test.c
embedded/tags/common/rtc/src/hal_rtc_lld.c
embedded/tags/common/core/src/bus_power.c
embedded/tags/common/core/src/handlers.c
embedded/tags/common/core/src/i2c_bus.c
embedded/tags/common/core/src/main.c
embedded/tags/common/core/src/monitor.c
embedded/tags/common/core/src/persistent.c
embedded/tags/common/rtc/src/rtc_rv3028.c
embedded/tags/common/rtc/src/rtc_test.c
embedded/tags/common/core/src/state_machine.c
embedded/tags/common/core/src/stm32adc.c
embedded/tags/common/core/src/stm32flash.c
embedded/tags/common/core/src/time.c
```

### Header Files

```text
embedded/tags/CompassTagAT25Breakout/Inc/config.h
embedded/tags/CompassTagAT25Breakout/Inc/custom.h
embedded/tags/CompassTagAT25Breakout/Inc/datalog.h
embedded/tags/CompassTagAT25Breakout/Inc/lis2du12.h
embedded/tags/CompassTagAT25Breakout/Inc/persistent.h
embedded/tags/CompassTagAT25Breakout/Inc/sensors.h
embedded/tags/CompassTagAT25Breakout/cfg/chconf.h
embedded/tags/CompassTagAT25Breakout/cfg/halconf.h
embedded/tags/CompassTagAT25Breakout/cfg/mcuconf.h
embedded/tags/common/sensors/mag/inc/ak09940a.h
embedded/tags/common/core/inc/app.h
embedded/tags/common/core/inc/adc.h
embedded/tags/common/core/inc/core_events.h
embedded/tags/common/core/inc/core_runtime.h
embedded/tags/common/core/inc/core_state.h
embedded/tags/common/core/inc/core_sync.h
embedded/tags/common/core/inc/core_types.h
embedded/tags/common/core/inc/debug_log.h
embedded/tags/common/core/inc/flash_internal.h
embedded/tags/common/core/inc/gpio_utils.h
embedded/tags/common/core/inc/i2c_bus.h
embedded/tags/common/core/inc/power.h
embedded/tags/common/core/inc/sensor_calibration.h
embedded/tags/common/core/inc/test_support.h
embedded/tags/common/core/inc/timekeeping.h
embedded/tags/common/rtc/inc/rtc_api.h
embedded/tags/common/storage/inc/external_flash.h
embedded/tags/common/rtc/inc/rv3028.h
```

## CompassTagAT25

### C Files

```text
embedded/tags/CompassTagAT25/src/config.c
embedded/tags/CompassTagAT25/src/datalog.c
embedded/tags/CompassTagAT25/src/lis2du12.c
embedded/tags/families/CompassTag/src/pwr.c
embedded/tags/families/CompassTag/src/sensors.c
embedded/tags/CompassTagAT25/src/state_run.c
embedded/tags/common/test/src/test.c
embedded/tags/common/sensors/src/sensor_io.c
embedded/tags/common/sensors/mag/src/ak09940a.c
embedded/tags/common/sensors/mag/src/ak09940a_shim.c
embedded/tags/common/sensors/mag/src/ak09940a_test.c
embedded/tags/families/CompassTag/src/lis2du12_test.c
embedded/tags/common/storage/src/at25xe.c
embedded/tags/common/storage/src/external_flash_test.c
embedded/tags/common/rtc/src/hal_rtc_lld.c
embedded/tags/common/core/src/bus_power.c
embedded/tags/common/core/src/handlers.c
embedded/tags/common/core/src/i2c_bus.c
embedded/tags/common/core/src/debug_log.c
embedded/tags/common/core/src/main.c
embedded/tags/common/core/src/monitor.c
embedded/tags/common/core/src/persistent.c
embedded/tags/common/rtc/src/rtc_rv3028.c
embedded/tags/common/rtc/src/rtc_test.c
embedded/tags/common/core/src/state_machine.c
embedded/tags/common/core/src/stm32adc.c
embedded/tags/common/core/src/stm32flash.c
embedded/tags/common/core/src/time.c
```

### Header Files

```text
embedded/tags/CompassTagAT25/Inc/config.h
embedded/tags/CompassTagAT25/Inc/custom.h
embedded/tags/CompassTagAT25/Inc/datalog.h
embedded/tags/CompassTagAT25/Inc/lis2du12.h
embedded/tags/CompassTagAT25/Inc/persistent.h
embedded/tags/CompassTagAT25/Inc/sensors.h
embedded/tags/CompassTagAT25/cfg/chconf.h
embedded/tags/CompassTagAT25/cfg/halconf.h
embedded/tags/CompassTagAT25/cfg/mcuconf.h
embedded/tags/common/sensors/mag/inc/ak09940a.h
embedded/tags/common/core/inc/app.h
embedded/tags/common/core/inc/adc.h
embedded/tags/common/core/inc/core_events.h
embedded/tags/common/core/inc/core_runtime.h
embedded/tags/common/core/inc/core_state.h
embedded/tags/common/core/inc/core_sync.h
embedded/tags/common/core/inc/core_types.h
embedded/tags/common/core/inc/debug_log.h
embedded/tags/common/core/inc/flash_internal.h
embedded/tags/common/core/inc/gpio_utils.h
embedded/tags/common/core/inc/i2c_bus.h
embedded/tags/common/core/inc/power.h
embedded/tags/common/core/inc/sensor_calibration.h
embedded/tags/common/core/inc/test_support.h
embedded/tags/common/core/inc/timekeeping.h
embedded/tags/common/rtc/inc/rtc_api.h
embedded/tags/common/storage/inc/external_flash.h
embedded/tags/common/rtc/inc/rv3028.h
```

## IMUTagBreakout

### C Files

```text
embedded/tags/IMUTagBreakout/src/ak09940.c
embedded/tags/IMUTagBreakout/src/ak09940_test.c
embedded/tags/IMUTagBreakout/src/config.c
embedded/tags/IMUTagBreakout/src/datalog.c
embedded/tags/common/sensors/src/sensor_io.c
embedded/tags/common/sensors/pressure/src/lps.c
embedded/tags/common/sensors/pressure/src/lps22hh.c
embedded/tags/IMUTagBreakout/src/lps22hh_test.c
embedded/tags/IMUTagBreakout/src/pwr.c
embedded/tags/IMUTagBreakout/src/sensors.c
embedded/tags/IMUTagBreakout/src/state_run.c
embedded/tags/IMUTagBreakout/src/stubs.c
embedded/tags/common/test/src/test.c
embedded/tags/common/rtc/src/hal_rtc_lld.c
embedded/tags/common/core/src/bus_power.c
embedded/tags/common/core/src/handlers.c
embedded/tags/common/core/src/i2c_bus.c
embedded/tags/common/core/src/debug_log.c
embedded/tags/common/core/src/main.c
embedded/tags/common/core/src/monitor.c
embedded/tags/common/storage/src/mx25l.c
embedded/tags/common/storage/src/external_flash_test.c
embedded/tags/common/core/src/persistent.c
embedded/tags/common/rtc/src/rtc_rv3028.c
embedded/tags/common/rtc/src/rtc_test.c
embedded/tags/common/core/src/state_machine.c
embedded/tags/common/core/src/stm32adc.c
embedded/tags/common/core/src/stm32flash.c
embedded/tags/common/core/src/time.c
```

### Header Files

```text
embedded/tags/IMUTagBreakout/Inc/ak09940.h
embedded/tags/IMUTagBreakout/Inc/config.h
embedded/tags/IMUTagBreakout/Inc/custom.h
embedded/tags/IMUTagBreakout/Inc/datalog.h
embedded/tags/IMUTagBreakout/Inc/lis2du12.h
embedded/tags/common/sensors/pressure/inc/lps22hh.h
embedded/tags/IMUTagBreakout/Inc/persistent.h
embedded/tags/IMUTagBreakout/Inc/sensors.h
embedded/tags/IMUTagBreakout/cfg/chconf.h
embedded/tags/IMUTagBreakout/cfg/halconf.h
embedded/tags/IMUTagBreakout/cfg/mcuconf.h
embedded/tags/common/sensors/mag/inc/ak09940a.h
embedded/tags/common/core/inc/app.h
embedded/tags/common/core/inc/adc.h
embedded/tags/common/core/inc/core_events.h
embedded/tags/common/core/inc/core_runtime.h
embedded/tags/common/core/inc/core_state.h
embedded/tags/common/core/inc/core_sync.h
embedded/tags/common/core/inc/core_types.h
embedded/tags/common/core/inc/debug_log.h
embedded/tags/common/core/inc/flash_internal.h
embedded/tags/common/core/inc/gpio_utils.h
embedded/tags/common/core/inc/i2c_bus.h
embedded/tags/common/core/inc/power.h
embedded/tags/common/core/inc/sensor_calibration.h
embedded/tags/common/core/inc/test_support.h
embedded/tags/common/core/inc/timekeeping.h
embedded/tags/common/rtc/inc/rtc_api.h
embedded/tags/common/storage/inc/external_flash.h
embedded/tags/common/sensors/inc/sensor_io.h
embedded/tags/common/rtc/inc/rv3028.h
```
