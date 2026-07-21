# Embedded Tag Build Sources

This file lists repo-local `.c` and `.h` files used by embedded tag targets
that build successfully in the current CMake build. The lists are extracted
from compiler dependency files, then filtered to include only files under
`embedded/tags`.

Generated build files, generated nanopb files, generated board files, ChibiOS
files, nanopb runtime files, toolchain headers, and other external files are
omitted. Local source overrides under `embedded/tags` are included.

Most active embedded tag target sections were rebuilt from
`/private/tmp/tag-software-embedded-testrefactor` after regenerating that build
tree. The BitTag and BitTag-legacy sections were refreshed from
`/private/tmp/tag-software-bittag-common` after the BitTag common-module
refactor.

## BitTag

### C Files

```text
embedded/tags/BitTag/src/bt_config.c
embedded/tags/BitTag/src/bt_state_run.c
embedded/tags/BitTag/src/datalog.c
embedded/tags/BitTag/src/devices.c
embedded/tags/common/core/src/bus_power.c
embedded/tags/common/core/src/device.c
embedded/tags/common/core/src/handlers.c
embedded/tags/common/core/src/i2c_bus.c
embedded/tags/common/core/src/main.c
embedded/tags/common/core/src/monitor.c
embedded/tags/common/core/src/persistent.c
embedded/tags/common/core/src/pwr.c
embedded/tags/common/core/src/spi_bus.c
embedded/tags/common/core/src/state_machine.c
embedded/tags/common/core/src/stm32adc.c
embedded/tags/common/core/src/stm32flash.c
embedded/tags/common/core/src/test.c
embedded/tags/common/core/src/time.c
embedded/tags/common/core/src/usart_bus.c
embedded/tags/common/rtc/src/hal_rtc_lld.c
embedded/tags/common/rtc/src/rtc_device.c
embedded/tags/common/rtc/src/rtc_rv3028.c
embedded/tags/common/rtc/src/rtc_test.c
embedded/tags/common/sensors/accel/src/ADXL362.c
embedded/tags/common/sensors/accel/src/adxl362_test.c
embedded/tags/common/sensors/src/sensor_io.c
```

### Header Files

```text
embedded/tags/BitTag/inc/config.h
embedded/tags/BitTag/inc/custom.h
embedded/tags/BitTag/inc/datalog.h
embedded/tags/BitTag/inc/devices.h
embedded/tags/BitTag/cfg/chconf.h
embedded/tags/BitTag/cfg/halconf.h
embedded/tags/BitTag/cfg/mcuconf.h
embedded/tags/common/core/inc/adc.h
embedded/tags/common/core/inc/app.h
embedded/tags/common/core/inc/bus_device.h
embedded/tags/common/core/inc/core_events.h
embedded/tags/common/core/inc/core_runtime.h
embedded/tags/common/core/inc/core_state.h
embedded/tags/common/core/inc/core_sync.h
embedded/tags/common/core/inc/core_types.h
embedded/tags/common/core/inc/debug_log.h
embedded/tags/common/core/inc/device.h
embedded/tags/common/core/inc/flash_internal.h
embedded/tags/common/core/inc/gpio_utils.h
embedded/tags/common/core/inc/i2c_bus.h
embedded/tags/common/core/inc/persistent.h
embedded/tags/common/core/inc/power.h
embedded/tags/common/core/inc/sensor_calibration.h
embedded/tags/common/core/inc/spi_bus.h
embedded/tags/common/core/inc/test_support.h
embedded/tags/common/core/inc/timekeeping.h
embedded/tags/common/core/inc/usart_bus.h
embedded/tags/common/rtc/inc/rtc_api.h
embedded/tags/common/rtc/inc/rtc_device.h
embedded/tags/common/rtc/inc/rv3028.h
embedded/tags/common/sensors/accel/inc/ADXL362.h
embedded/tags/common/sensors/inc/sensor_io.h
```

## BitTag-legacy

### C Files

```text
embedded/tags/BitTag-legacy/src/ADXL362.c
embedded/tags/BitTag-legacy/src/adxl362_test.c
embedded/tags/BitTag-legacy/src/bt_config.c
embedded/tags/BitTag-legacy/src/bt_state_run.c
embedded/tags/BitTag-legacy/src/hal_lld.c
embedded/tags/BitTag-legacy/src/hal_rtc_lld.c
embedded/tags/BitTag-legacy/src/main.c
embedded/tags/BitTag-legacy/src/monitor.c
embedded/tags/BitTag-legacy/src/persistent.c
embedded/tags/BitTag-legacy/src/pwr.c
embedded/tags/BitTag-legacy/src/rtc_rv3028.c
embedded/tags/BitTag-legacy/src/rtc_test.c
embedded/tags/BitTag-legacy/src/state_machine.c
embedded/tags/BitTag-legacy/src/test.c
embedded/tags/BitTag-legacy/src/time.c
embedded/tags/common/core/src/bus_power.c
embedded/tags/common/core/src/device.c
embedded/tags/common/core/src/handlers.c
embedded/tags/common/core/src/i2c_bus.c
embedded/tags/common/core/src/spi_bus.c
embedded/tags/common/core/src/stm32adc.c
embedded/tags/common/core/src/stm32flash.c
embedded/tags/common/core/src/usart_bus.c
```

### Header Files

```text
embedded/tags/BitTag-legacy/inc/ADXL362.h
embedded/tags/BitTag-legacy/inc/custom.h
embedded/tags/BitTag-legacy/inc/rtc_api.h
embedded/tags/BitTag-legacy/inc/rv3028.h
embedded/tags/BitTag-legacy/cfg/chconf.h
embedded/tags/BitTag-legacy/cfg/halconf.h
embedded/tags/BitTag-legacy/cfg/mcuconf.h
embedded/tags/common/core/inc/adc.h
embedded/tags/common/core/inc/app.h
embedded/tags/common/core/inc/bus_device.h
embedded/tags/common/core/inc/core_events.h
embedded/tags/common/core/inc/core_runtime.h
embedded/tags/common/core/inc/core_state.h
embedded/tags/common/core/inc/core_sync.h
embedded/tags/common/core/inc/core_types.h
embedded/tags/common/core/inc/debug_log.h
embedded/tags/common/core/inc/device.h
embedded/tags/common/core/inc/flash_internal.h
embedded/tags/common/core/inc/gpio_utils.h
embedded/tags/common/core/inc/i2c_bus.h
embedded/tags/common/core/inc/persistent.h
embedded/tags/common/core/inc/power.h
embedded/tags/common/core/inc/sensor_calibration.h
embedded/tags/common/core/inc/spi_bus.h
embedded/tags/common/core/inc/test_support.h
embedded/tags/common/core/inc/timekeeping.h
embedded/tags/common/core/inc/usart_bus.h
embedded/tags/common/inc/config.h
embedded/tags/common/sensors/inc/sensor_io.h
embedded/tags/common/sensors/pressure/inc/lps.h
```

## BitTagNG

### C Files

```text
embedded/tags/BitTagNG/src/datalog.c
embedded/tags/BitTagNG/src/state_run.c
embedded/tags/families/BitTagNG/src/config.c
embedded/tags/families/BitTagNG/src/devices.c
embedded/tags/families/BitTagNG/src/sensors.c
embedded/tags/common/core/src/bus_power.c
embedded/tags/common/core/src/device.c
embedded/tags/common/core/src/handlers.c
embedded/tags/common/core/src/i2c_bus.c
embedded/tags/common/core/src/main.c
embedded/tags/common/core/src/monitor.c
embedded/tags/common/core/src/persistent.c
embedded/tags/common/core/src/pwr.c
embedded/tags/common/core/src/spi_bus.c
embedded/tags/common/core/src/state_machine.c
embedded/tags/common/core/src/stm32adc.c
embedded/tags/common/core/src/stm32flash.c
embedded/tags/common/core/src/test.c
embedded/tags/common/core/src/time.c
embedded/tags/common/core/src/usart_bus.c
embedded/tags/common/rtc/src/hal_rtc_lld.c
embedded/tags/common/rtc/src/rtc_device.c
embedded/tags/common/rtc/src/rtc_rv3028.c
embedded/tags/common/rtc/src/rtc_test.c
embedded/tags/common/sensors/accel/src/ADXL367.c
embedded/tags/common/sensors/accel/src/adxl367_test.c
embedded/tags/common/sensors/src/sensor_io.c
embedded/tags/common/storage/src/at25xe.c
embedded/tags/common/storage/src/external_flash_test.c
embedded/tags/common/storage/src/storage_flash.c
```

### Header Files

```text
embedded/tags/BitTagNG/inc/custom.h
embedded/tags/families/BitTagNG/inc/config.h
embedded/tags/families/BitTagNG/inc/datalog.h
embedded/tags/families/BitTagNG/inc/devices.h
embedded/tags/families/BitTagNG/inc/persistent.h
embedded/tags/families/BitTagNG/inc/sensors.h
embedded/tags/families/BitTagNG/cfg/chconf.h
embedded/tags/families/BitTagNG/cfg/halconf.h
embedded/tags/families/BitTagNG/cfg/mcuconf.h
embedded/tags/common/core/inc/adc.h
embedded/tags/common/core/inc/app.h
embedded/tags/common/core/inc/bus_device.h
embedded/tags/common/core/inc/core_events.h
embedded/tags/common/core/inc/core_runtime.h
embedded/tags/common/core/inc/core_state.h
embedded/tags/common/core/inc/core_sync.h
embedded/tags/common/core/inc/core_types.h
embedded/tags/common/core/inc/debug_log.h
embedded/tags/common/core/inc/device.h
embedded/tags/common/core/inc/flash_internal.h
embedded/tags/common/core/inc/gpio_utils.h
embedded/tags/common/core/inc/i2c_bus.h
embedded/tags/common/core/inc/power.h
embedded/tags/common/core/inc/sensor_calibration.h
embedded/tags/common/core/inc/spi_bus.h
embedded/tags/common/core/inc/test_support.h
embedded/tags/common/core/inc/timekeeping.h
embedded/tags/common/core/inc/usart_bus.h
embedded/tags/common/rtc/inc/rtc_api.h
embedded/tags/common/rtc/inc/rtc_device.h
embedded/tags/common/rtc/inc/rv3028.h
embedded/tags/common/sensors/accel/inc/ADXL367.h
embedded/tags/common/sensors/inc/sensor_io.h
embedded/tags/common/storage/inc/at25xe.h
embedded/tags/common/storage/inc/storage_device.h
embedded/tags/common/storage/inc/storage_flash.h
embedded/tags/common/storage/inc/storage_spi.h
```

## PresTag

### C Files

```text
embedded/tags/families/PresTag/src/config.c
embedded/tags/families/PresTag/src/datalog.c
embedded/tags/families/PresTag/src/devices.c
embedded/tags/families/PresTag/src/state_run.c
embedded/tags/common/storage/src/at25xe.c
embedded/tags/common/storage/src/external_flash_test.c
embedded/tags/common/rtc/src/hal_rtc_lld.c
embedded/tags/common/core/src/bus_power.c
embedded/tags/common/core/src/handlers.c
embedded/tags/common/core/src/i2c_bus.c
embedded/tags/common/sensors/src/sensor_io.c
embedded/tags/common/sensors/pressure/src/pressure_device.c
embedded/tags/common/sensors/pressure/src/lps27.c
embedded/tags/common/sensors/pressure/src/lps27_test.c
embedded/tags/common/core/src/main.c
embedded/tags/common/core/src/monitor.c
embedded/tags/common/core/src/persistent.c
embedded/tags/common/rtc/src/rtc_device.c
embedded/tags/common/rtc/src/rtc_rv3028.c
embedded/tags/common/rtc/src/rtc_test.c
embedded/tags/common/core/src/state_machine.c
embedded/tags/common/core/src/stm32adc.c
embedded/tags/common/core/src/stm32flash.c
embedded/tags/common/core/src/usart_bus.c
embedded/tags/common/core/src/test.c
embedded/tags/common/core/src/time.c
```

### Header Files

```text
include/prestag_log_format.h
embedded/tags/PresTag/inc/custom.h
embedded/tags/families/PresTag/inc/config.h
embedded/tags/families/PresTag/inc/datalog.h
embedded/tags/families/PresTag/inc/devices.h
embedded/tags/families/PresTag/inc/persistent.h
embedded/tags/families/PresTag/cfg/chconf.h
embedded/tags/families/PresTag/cfg/halconf.h
embedded/tags/families/PresTag/cfg/mcuconf.h
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
embedded/tags/common/core/inc/usart_bus.h
embedded/tags/common/rtc/inc/rtc_api.h
embedded/tags/common/sensors/accel/inc/lis2dtw12.h
embedded/tags/common/sensors/inc/sensor_io.h
embedded/tags/common/sensors/pressure/inc/lps.h
embedded/tags/common/sensors/pressure/inc/lps27hhw.h
embedded/tags/common/sensors/mag/inc/mmc5633.h
embedded/tags/common/sensors/light/inc/opt3002.h
embedded/tags/common/rtc/inc/rv3028.h
```

## PresTagRaw

### C Files

```text
embedded/tags/families/PresTag/src/config.c
embedded/tags/families/PresTag/src/datalog.c
embedded/tags/families/PresTag/src/devices.c
embedded/tags/families/PresTag/src/state_run.c
embedded/tags/common/storage/src/at25xe.c
embedded/tags/common/storage/src/external_flash_test.c
embedded/tags/common/rtc/src/hal_rtc_lld.c
embedded/tags/common/core/src/bus_power.c
embedded/tags/common/core/src/handlers.c
embedded/tags/common/core/src/i2c_bus.c
embedded/tags/common/sensors/src/sensor_io.c
embedded/tags/common/sensors/pressure/src/pressure_device.c
embedded/tags/common/sensors/pressure/src/lps27.c
embedded/tags/common/sensors/pressure/src/lps27_test.c
embedded/tags/common/core/src/main.c
embedded/tags/common/core/src/monitor.c
embedded/tags/common/core/src/persistent.c
embedded/tags/common/rtc/src/rtc_device.c
embedded/tags/common/rtc/src/rtc_rv3028.c
embedded/tags/common/rtc/src/rtc_test.c
embedded/tags/common/core/src/state_machine.c
embedded/tags/common/core/src/stm32adc.c
embedded/tags/common/core/src/stm32flash.c
embedded/tags/common/core/src/usart_bus.c
embedded/tags/common/core/src/test.c
embedded/tags/common/core/src/time.c
```

### Header Files

```text
include/prestag_log_format.h
embedded/tags/PresTagRaw/inc/custom.h
embedded/tags/families/PresTag/inc/config.h
embedded/tags/families/PresTag/inc/datalog.h
embedded/tags/families/PresTag/inc/devices.h
embedded/tags/families/PresTag/inc/persistent.h
embedded/tags/families/PresTag/cfg/chconf.h
embedded/tags/families/PresTag/cfg/halconf.h
embedded/tags/families/PresTag/cfg/mcuconf.h
embedded/tags/common/sensors/pressure/inc/lps.h
embedded/tags/common/sensors/pressure/inc/lps27hhw.h
embedded/tags/common/storage/inc/at25xe.h
embedded/tags/common/storage/inc/storage_flash.h
embedded/tags/common/rtc/inc/rtc_api.h
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
embedded/tags/common/sensors/pressure/src/pressure_device.c
embedded/tags/common/sensors/pressure/src/lps27.c
embedded/tags/common/sensors/pressure/src/lps27_test.c
embedded/tags/common/core/src/main.c
embedded/tags/common/core/src/monitor.c
embedded/tags/common/core/src/persistent.c
embedded/tags/common/rtc/src/rtc_device.c
embedded/tags/common/rtc/src/rtc_rv3028.c
embedded/tags/common/rtc/src/rtc_test.c
embedded/tags/common/core/src/state_machine.c
embedded/tags/common/core/src/stm32adc.c
embedded/tags/common/core/src/stm32flash.c
embedded/tags/common/core/src/usart_bus.c
embedded/tags/common/core/src/test.c
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
embedded/tags/common/core/inc/usart_bus.h
embedded/tags/common/rtc/inc/rtc_api.h
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
embedded/tags/families/CompassTag/src/lis2du12.c
embedded/tags/families/CompassTag/src/pwr.c
embedded/tags/families/CompassTag/src/sensors.c
embedded/tags/CompassTag/src/state_run.c
embedded/tags/common/core/src/test.c
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
embedded/tags/common/rtc/src/rtc_device.c
embedded/tags/common/rtc/src/rtc_rv3028.c
embedded/tags/common/rtc/src/rtc_test.c
embedded/tags/common/core/src/state_machine.c
embedded/tags/common/core/src/stm32adc.c
embedded/tags/common/core/src/stm32flash.c
embedded/tags/common/core/src/usart_bus.c
embedded/tags/common/core/src/time.c
```

### Header Files

```text
embedded/tags/CompassTag/Inc/config.h
embedded/tags/CompassTag/Inc/custom.h
embedded/tags/CompassTag/Inc/datalog.h
embedded/tags/families/CompassTag/inc/lis2du12.h
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
embedded/tags/common/core/inc/usart_bus.h
embedded/tags/common/rtc/inc/rtc_api.h
embedded/tags/common/rtc/inc/rv3028.h
```

## CompassTagAT25Breakout

### C Files

```text
embedded/tags/CompassTagAT25Breakout/src/config.c
embedded/tags/CompassTagAT25Breakout/src/datalog.c
embedded/tags/families/CompassTag/src/lis2du12.c
embedded/tags/families/CompassTag/src/pwr.c
embedded/tags/families/CompassTag/src/sensors.c
embedded/tags/CompassTagAT25Breakout/src/state_run.c
embedded/tags/common/core/src/test.c
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
embedded/tags/common/rtc/src/rtc_device.c
embedded/tags/common/rtc/src/rtc_rv3028.c
embedded/tags/common/rtc/src/rtc_test.c
embedded/tags/common/core/src/state_machine.c
embedded/tags/common/core/src/stm32adc.c
embedded/tags/common/core/src/stm32flash.c
embedded/tags/common/core/src/usart_bus.c
embedded/tags/common/core/src/time.c
```

### Header Files

```text
embedded/tags/CompassTagAT25Breakout/Inc/config.h
embedded/tags/CompassTagAT25Breakout/Inc/custom.h
embedded/tags/CompassTagAT25Breakout/Inc/datalog.h
embedded/tags/families/CompassTag/inc/lis2du12.h
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
embedded/tags/common/core/inc/usart_bus.h
embedded/tags/common/rtc/inc/rtc_api.h
embedded/tags/common/rtc/inc/rv3028.h
```

## CompassTagAT25

### C Files

```text
embedded/tags/CompassTagAT25/src/config.c
embedded/tags/CompassTagAT25/src/datalog.c
embedded/tags/families/CompassTag/src/lis2du12.c
embedded/tags/families/CompassTag/src/pwr.c
embedded/tags/families/CompassTag/src/sensors.c
embedded/tags/CompassTagAT25/src/state_run.c
embedded/tags/common/core/src/test.c
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
embedded/tags/common/rtc/src/rtc_device.c
embedded/tags/common/rtc/src/rtc_rv3028.c
embedded/tags/common/rtc/src/rtc_test.c
embedded/tags/common/core/src/state_machine.c
embedded/tags/common/core/src/stm32adc.c
embedded/tags/common/core/src/stm32flash.c
embedded/tags/common/core/src/usart_bus.c
embedded/tags/common/core/src/time.c
```

### Header Files

```text
embedded/tags/CompassTagAT25/Inc/config.h
embedded/tags/CompassTagAT25/Inc/custom.h
embedded/tags/CompassTagAT25/Inc/datalog.h
embedded/tags/families/CompassTag/inc/lis2du12.h
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
embedded/tags/common/core/inc/usart_bus.h
embedded/tags/common/rtc/inc/rtc_api.h
embedded/tags/common/rtc/inc/rv3028.h
```

## IMUTagBreakout

### C Files

```text
embedded/tags/families/IMUTag/src/config.c
embedded/tags/families/IMUTag/src/datalog.c
embedded/tags/families/IMUTag/src/devices.c
embedded/tags/families/IMUTag/src/sensors.c
embedded/tags/families/IMUTag/src/state_run.c
embedded/tags/families/IMUTag/src/stubs.c
embedded/tags/common/core/src/bus_power.c
embedded/tags/common/core/src/debug_log.c
embedded/tags/common/core/src/device.c
embedded/tags/common/core/src/handlers.c
embedded/tags/common/core/src/i2c_bus.c
embedded/tags/common/core/src/main.c
embedded/tags/common/core/src/monitor.c
embedded/tags/common/core/src/persistent.c
embedded/tags/common/core/src/pwr.c
embedded/tags/common/core/src/spi_bus.c
embedded/tags/common/core/src/state_machine.c
embedded/tags/common/core/src/stm32adc.c
embedded/tags/common/core/src/stm32flash.c
embedded/tags/common/core/src/test.c
embedded/tags/common/core/src/time.c
embedded/tags/common/core/src/usart_bus.c
embedded/tags/common/rtc/src/hal_rtc_lld.c
embedded/tags/common/rtc/src/rtc_device.c
embedded/tags/common/rtc/src/rtc_rv3028.c
embedded/tags/common/rtc/src/rtc_test.c
embedded/tags/common/sensors/imu/lsm6dsv16x.c
embedded/tags/common/sensors/imu/lsm6dsv16x_test.c
embedded/tags/common/sensors/mag/src/ak09940a.c
embedded/tags/common/sensors/mag/src/ak09940a_shim.c
embedded/tags/common/sensors/mag/src/ak09940a_test.c
embedded/tags/common/sensors/pressure/src/lps22hh.c
embedded/tags/common/sensors/pressure/src/lps22hh_test.c
embedded/tags/common/sensors/pressure/src/pressure_device.c
embedded/tags/common/sensors/src/sensor_io.c
embedded/tags/common/storage/src/external_flash_test.c
embedded/tags/common/storage/src/mx25l.c
embedded/tags/common/storage/src/storage_flash.c
```

### Header Files

```text
embedded/tags/IMUTagBreakout/inc/custom.h
embedded/tags/families/IMUTag/inc/config.h
embedded/tags/families/IMUTag/inc/datalog.h
embedded/tags/families/IMUTag/inc/devices.h
embedded/tags/families/IMUTag/inc/persistent.h
embedded/tags/families/IMUTag/inc/sensors.h
embedded/tags/families/IMUTag/cfg/chconf.h
embedded/tags/families/IMUTag/cfg/halconf.h
embedded/tags/families/IMUTag/cfg/mcuconf.h
embedded/tags/common/core/inc/adc.h
embedded/tags/common/core/inc/app.h
embedded/tags/common/core/inc/bus_device.h
embedded/tags/common/core/inc/core_events.h
embedded/tags/common/core/inc/core_runtime.h
embedded/tags/common/core/inc/core_state.h
embedded/tags/common/core/inc/core_sync.h
embedded/tags/common/core/inc/core_types.h
embedded/tags/common/core/inc/debug_log.h
embedded/tags/common/core/inc/device.h
embedded/tags/common/core/inc/flash_internal.h
embedded/tags/common/core/inc/gpio_utils.h
embedded/tags/common/core/inc/i2c_bus.h
embedded/tags/common/core/inc/power.h
embedded/tags/common/core/inc/sensor_calibration.h
embedded/tags/common/core/inc/spi_bus.h
embedded/tags/common/core/inc/test_support.h
embedded/tags/common/core/inc/timekeeping.h
embedded/tags/common/core/inc/usart_bus.h
embedded/tags/common/inc/stm32_lse.inc
embedded/tags/common/rtc/inc/rtc_api.h
embedded/tags/common/rtc/inc/rtc_device.h
embedded/tags/common/rtc/inc/rv3028.h
embedded/tags/common/sensors/inc/sensor_io.h
embedded/tags/common/sensors/imu/lsm6dsv16x.h
embedded/tags/common/sensors/imu/lsm6dsv16x_regs.h
embedded/tags/common/sensors/mag/inc/ak09940a.h
embedded/tags/common/sensors/pressure/inc/lps.h
embedded/tags/common/sensors/pressure/inc/lps22hh.h
embedded/tags/common/storage/inc/storage_device.h
embedded/tags/common/storage/inc/storage_flash.h
embedded/tags/common/storage/inc/storage_mx25l.h
embedded/tags/common/storage/inc/storage_spi.h
```

## IMUTagbmm350

### C Files

```text
embedded/tags/families/IMUTag/src/config.c
embedded/tags/families/IMUTag/src/datalog.c
embedded/tags/families/IMUTag/src/devices.c
embedded/tags/families/IMUTag/src/sensors.c
embedded/tags/families/IMUTag/src/state_run.c
embedded/tags/families/IMUTag/src/stubs.c
embedded/tags/common/core/src/bus_power.c
embedded/tags/common/core/src/debug_log.c
embedded/tags/common/core/src/device.c
embedded/tags/common/core/src/handlers.c
embedded/tags/common/core/src/i2c_bus.c
embedded/tags/common/core/src/main.c
embedded/tags/common/core/src/monitor.c
embedded/tags/common/core/src/persistent.c
embedded/tags/common/core/src/pwr.c
embedded/tags/common/core/src/spi_bus.c
embedded/tags/common/core/src/state_machine.c
embedded/tags/common/core/src/stm32adc.c
embedded/tags/common/core/src/stm32flash.c
embedded/tags/common/core/src/tag_soft_i2c.c
embedded/tags/common/core/src/test.c
embedded/tags/common/core/src/time.c
embedded/tags/common/core/src/usart_bus.c
embedded/tags/common/rtc/src/hal_rtc_lld.c
embedded/tags/common/rtc/src/rtc_device.c
embedded/tags/common/rtc/src/rtc_rv3028.c
embedded/tags/common/rtc/src/rtc_test.c
embedded/tags/common/sensors/imu/lsm6dsv16x.c
embedded/tags/common/sensors/imu/lsm6dsv16x_test.c
embedded/tags/common/sensors/mag/src/bmm350_tag.c
embedded/tags/common/sensors/mag/src/bmm350_test.c
embedded/tags/common/sensors/pressure/src/lps22hh.c
embedded/tags/common/sensors/pressure/src/lps22hh_test.c
embedded/tags/common/sensors/pressure/src/pressure_device.c
embedded/tags/common/sensors/src/sensor_io.c
embedded/tags/common/storage/src/external_flash_test.c
embedded/tags/common/storage/src/mx25l.c
embedded/tags/common/storage/src/storage_flash.c
```

### Header Files

```text
embedded/tags/IMUTagbmm350/inc/custom.h
embedded/tags/families/IMUTag/inc/config.h
embedded/tags/families/IMUTag/inc/datalog.h
embedded/tags/families/IMUTag/inc/devices.h
embedded/tags/families/IMUTag/inc/persistent.h
embedded/tags/families/IMUTag/inc/sensors.h
embedded/tags/families/IMUTag/cfg/chconf.h
embedded/tags/families/IMUTag/cfg/halconf.h
embedded/tags/families/IMUTag/cfg/mcuconf.h
embedded/tags/common/core/inc/adc.h
embedded/tags/common/core/inc/app.h
embedded/tags/common/core/inc/bus_device.h
embedded/tags/common/core/inc/core_events.h
embedded/tags/common/core/inc/core_runtime.h
embedded/tags/common/core/inc/core_state.h
embedded/tags/common/core/inc/core_sync.h
embedded/tags/common/core/inc/core_types.h
embedded/tags/common/core/inc/debug_log.h
embedded/tags/common/core/inc/device.h
embedded/tags/common/core/inc/flash_internal.h
embedded/tags/common/core/inc/gpio_utils.h
embedded/tags/common/core/inc/i2c_bus.h
embedded/tags/common/core/inc/power.h
embedded/tags/common/core/inc/sensor_calibration.h
embedded/tags/common/core/inc/spi_bus.h
embedded/tags/common/core/inc/test_support.h
embedded/tags/common/core/inc/timekeeping.h
embedded/tags/common/core/inc/usart_bus.h
embedded/tags/common/inc/stm32_lse.inc
embedded/tags/common/rtc/inc/rtc_api.h
embedded/tags/common/rtc/inc/rtc_device.h
embedded/tags/common/rtc/inc/rv3028.h
embedded/tags/common/sensors/inc/sensor_io.h
embedded/tags/common/sensors/imu/lsm6dsv16x.h
embedded/tags/common/sensors/imu/lsm6dsv16x_regs.h
embedded/tags/common/sensors/mag/inc/bmm350_tag.h
embedded/tags/common/sensors/pressure/inc/lps.h
embedded/tags/common/sensors/pressure/inc/lps22hh.h
embedded/tags/common/storage/inc/storage_device.h
embedded/tags/common/storage/inc/storage_flash.h
embedded/tags/common/storage/inc/storage_mx25l.h
embedded/tags/common/storage/inc/storage_spi.h
```

## IMUTagU375

### C Files

```text
embedded/tags/IMUTagU375/src/hal_rtc_lld.c
embedded/tags/families/IMUTag/src/config.c
embedded/tags/families/IMUTag/src/datalog.c
embedded/tags/families/IMUTag/src/devices.c
embedded/tags/families/IMUTag/src/sensors.c
embedded/tags/families/IMUTag/src/state_run.c
embedded/tags/families/IMUTag/src/stubs.c
embedded/tags/common/core/src/bus_power.c
embedded/tags/common/core/src/debug_log.c
embedded/tags/common/core/src/device.c
embedded/tags/common/core/src/handlers.c
embedded/tags/common/core/src/i2c_bus.c
embedded/tags/common/core/src/main.c
embedded/tags/common/core/src/monitor.c
embedded/tags/common/core/src/persistent.c
embedded/tags/common/core/src/pwr.c
embedded/tags/common/core/src/spi_bus.c
embedded/tags/common/core/src/state_machine.c
embedded/tags/common/core/src/stm32adc.c
embedded/tags/common/core/src/stm32flash.c
embedded/tags/common/core/src/test.c
embedded/tags/common/core/src/time.c
embedded/tags/common/core/src/usart_bus.c
embedded/tags/common/rtc/src/rtc_device.c
embedded/tags/common/rtc/src/rtc_rv3028.c
embedded/tags/common/rtc/src/rtc_test.c
embedded/tags/common/sensors/imu/lsm6dsv16x.c
embedded/tags/common/sensors/imu/lsm6dsv16x_test.c
embedded/tags/common/sensors/mag/src/ak09940a.c
embedded/tags/common/sensors/mag/src/ak09940a_shim.c
embedded/tags/common/sensors/mag/src/ak09940a_test.c
embedded/tags/common/sensors/pressure/src/lps22hh.c
embedded/tags/common/sensors/pressure/src/lps22hh_test.c
embedded/tags/common/sensors/pressure/src/pressure_device.c
embedded/tags/common/sensors/src/sensor_io.c
embedded/tags/common/storage/src/external_flash_test.c
embedded/tags/common/storage/src/mx25u12843.c
embedded/tags/common/storage/src/storage_flash.c
```

### Header Files

```text
embedded/tags/IMUTagU375/inc/custom.h
embedded/tags/IMUTagU375/cfg/chconf.h
embedded/tags/IMUTagU375/cfg/halconf.h
embedded/tags/IMUTagU375/cfg/mcuconf.h
embedded/tags/families/IMUTag/inc/config.h
embedded/tags/families/IMUTag/inc/datalog.h
embedded/tags/families/IMUTag/inc/devices.h
embedded/tags/families/IMUTag/inc/persistent.h
embedded/tags/families/IMUTag/inc/sensors.h
embedded/tags/common/core/inc/adc.h
embedded/tags/common/core/inc/app.h
embedded/tags/common/core/inc/bus_device.h
embedded/tags/common/core/inc/core_events.h
embedded/tags/common/core/inc/core_runtime.h
embedded/tags/common/core/inc/core_state.h
embedded/tags/common/core/inc/core_sync.h
embedded/tags/common/core/inc/core_types.h
embedded/tags/common/core/inc/debug_log.h
embedded/tags/common/core/inc/device.h
embedded/tags/common/core/inc/flash_internal.h
embedded/tags/common/core/inc/gpio_utils.h
embedded/tags/common/core/inc/i2c_bus.h
embedded/tags/common/core/inc/power.h
embedded/tags/common/core/inc/sensor_calibration.h
embedded/tags/common/core/inc/spi_bus.h
embedded/tags/common/core/inc/test_support.h
embedded/tags/common/core/inc/timekeeping.h
embedded/tags/common/core/inc/usart_bus.h
embedded/tags/common/inc/stm32_lse.inc
embedded/tags/common/rtc/inc/rtc_api.h
embedded/tags/common/rtc/inc/rtc_device.h
embedded/tags/common/rtc/inc/rv3028.h
embedded/tags/common/sensors/inc/sensor_io.h
embedded/tags/common/sensors/imu/lsm6dsv16x.h
embedded/tags/common/sensors/imu/lsm6dsv16x_regs.h
embedded/tags/common/sensors/mag/inc/ak09940a.h
embedded/tags/common/sensors/pressure/inc/lps.h
embedded/tags/common/sensors/pressure/inc/lps22hh.h
embedded/tags/common/storage/inc/storage_device.h
embedded/tags/common/storage/inc/storage_flash.h
embedded/tags/common/storage/inc/storage_mx25u12843.h
embedded/tags/common/storage/inc/storage_spi.h
```

## IMUTagU3bmm350

### C Files

```text
embedded/tags/IMUTagU3bmm350/src/hal_rtc_lld.c
embedded/tags/families/IMUTag/src/config.c
embedded/tags/families/IMUTag/src/datalog.c
embedded/tags/families/IMUTag/src/devices.c
embedded/tags/families/IMUTag/src/sensors.c
embedded/tags/families/IMUTag/src/state_run.c
embedded/tags/families/IMUTag/src/stubs.c
embedded/tags/common/core/src/bus_power.c
embedded/tags/common/core/src/debug_log.c
embedded/tags/common/core/src/device.c
embedded/tags/common/core/src/handlers.c
embedded/tags/common/core/src/i2c_bus.c
embedded/tags/common/core/src/main.c
embedded/tags/common/core/src/monitor.c
embedded/tags/common/core/src/persistent.c
embedded/tags/common/core/src/pwr.c
embedded/tags/common/core/src/spi_bus.c
embedded/tags/common/core/src/state_machine.c
embedded/tags/common/core/src/stm32adc.c
embedded/tags/common/core/src/stm32flash.c
embedded/tags/common/core/src/tag_soft_i2c.c
embedded/tags/common/core/src/test.c
embedded/tags/common/core/src/time.c
embedded/tags/common/core/src/usart_bus.c
embedded/tags/common/rtc/src/rtc_device.c
embedded/tags/common/rtc/src/rtc_rv3028.c
embedded/tags/common/rtc/src/rtc_test.c
embedded/tags/common/sensors/imu/lsm6dsv16x.c
embedded/tags/common/sensors/imu/lsm6dsv16x_test.c
embedded/tags/common/sensors/mag/src/bmm350_tag.c
embedded/tags/common/sensors/mag/src/bmm350_test.c
embedded/tags/common/sensors/pressure/src/lps22hh.c
embedded/tags/common/sensors/pressure/src/lps22hh_test.c
embedded/tags/common/sensors/pressure/src/pressure_device.c
embedded/tags/common/sensors/src/sensor_io.c
embedded/tags/common/storage/src/external_flash_test.c
embedded/tags/common/storage/src/mx25u12843.c
embedded/tags/common/storage/src/storage_flash.c
```

### Header Files

```text
embedded/tags/IMUTagU3bmm350/inc/custom.h
embedded/tags/IMUTagU3bmm350/cfg/chconf.h
embedded/tags/IMUTagU3bmm350/cfg/halconf.h
embedded/tags/IMUTagU3bmm350/cfg/mcuconf.h
embedded/tags/families/IMUTag/inc/config.h
embedded/tags/families/IMUTag/inc/datalog.h
embedded/tags/families/IMUTag/inc/devices.h
embedded/tags/families/IMUTag/inc/persistent.h
embedded/tags/families/IMUTag/inc/sensors.h
embedded/tags/common/core/inc/adc.h
embedded/tags/common/core/inc/app.h
embedded/tags/common/core/inc/bus_device.h
embedded/tags/common/core/inc/core_events.h
embedded/tags/common/core/inc/core_runtime.h
embedded/tags/common/core/inc/core_state.h
embedded/tags/common/core/inc/core_sync.h
embedded/tags/common/core/inc/core_types.h
embedded/tags/common/core/inc/debug_log.h
embedded/tags/common/core/inc/device.h
embedded/tags/common/core/inc/flash_internal.h
embedded/tags/common/core/inc/gpio_utils.h
embedded/tags/common/core/inc/i2c_bus.h
embedded/tags/common/core/inc/power.h
embedded/tags/common/core/inc/sensor_calibration.h
embedded/tags/common/core/inc/spi_bus.h
embedded/tags/common/core/inc/test_support.h
embedded/tags/common/core/inc/timekeeping.h
embedded/tags/common/core/inc/usart_bus.h
embedded/tags/common/inc/stm32_lse.inc
embedded/tags/common/rtc/inc/rtc_api.h
embedded/tags/common/rtc/inc/rtc_device.h
embedded/tags/common/rtc/inc/rv3028.h
embedded/tags/common/sensors/inc/sensor_io.h
embedded/tags/common/sensors/imu/lsm6dsv16x.h
embedded/tags/common/sensors/imu/lsm6dsv16x_regs.h
embedded/tags/common/sensors/mag/inc/bmm350_tag.h
embedded/tags/common/sensors/pressure/inc/lps.h
embedded/tags/common/sensors/pressure/inc/lps22hh.h
embedded/tags/common/storage/inc/storage_device.h
embedded/tags/common/storage/inc/storage_flash.h
embedded/tags/common/storage/inc/storage_mx25u12843.h
embedded/tags/common/storage/inc/storage_spi.h
```
