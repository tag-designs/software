# Core Runtime

`core` owns the tag runtime that is not specific to one external device. It is
compiled by the `tag_core` module and is the default implementation used by
active tags unless a tag provides a same-named local override.

## Main Blocks

- `main.c`, `state_machine.c`: shared execution loop and monitor-controlled
  state transitions.
- `handlers.c`, `monitor.c`: protobuf request/ack handling and monitor-facing
  commands.
- `persistent.c`, `stm32flash.c`: persistent state stored in internal STM32
  flash.
- `time.c`: RTC/ticker/alarm helpers and low-power sleep entry.
- `pwr.c`: default power policy for tags that do not provide local/family
  power code.
- `bus_power.c`: board-line descriptor helpers for SPI/I2C device power,
  bus-session setup, standby pulls, and Stop2 bus suspend/resume orchestration.
- `spi_bus.c`, `i2c_bus.c`, `usart_bus.c`: low-level byte/register transfers
  that are useful outside a single sensor family. SPI and USART also own their
  controller setup, active-state tracking, and Stop2 suspend/resume mechanics.
- `debug_log.c`: optional monitor-readable debug-message buffer selected by
  the `debug_log` module.

## Power And Bus Ownership

Power lifetime and bus lifetime are intentionally separate. For SPI devices:

- `tagSpiDevicePowerOn/Off()` handles optional switched device power and safe
  pin states.
- `tagSpiBusBegin/End()` handles the mutex, alternate functions, and SPI
  controller enable/disable.
- `tagSpiDevicePrepareSleep()` applies standby pull policy before deep sleep.

Short Stop2 sleeps call `tagDisableActiveBusesForStop()` before entering Stop2
and `tagEnableActiveBusesAfterStop()` after wake. `bus_power.c` coordinates
those calls, while `spi_bus.c` and `usart_bus.c` own the controller-specific
active/suspended state and register bit changes. The stop path suspends any
active SPI1 or USART2 controller without changing device power, chip-select
ownership, or pin alternate-function setup. Code that bypasses
`tagSpiBusBegin/End()` must call `tagMarkSpi1On()` and `tagMarkSpi1Off()`
itself. Tag-local synchronous-USART setup must do the same with
`tagMarkUsart2On()` and `tagMarkUsart2Off()`.

## Header Guidance

`app.h` is retained as a compatibility umbrella for older code. New common code
should include the specific subsystem header it needs:

- `core_events.h`, `core_state.h`, `core_runtime.h`, `core_sync.h`
- `power.h`, `timekeeping.h`, `persistent.h`
- `adc.h`, `flash_internal.h`, `debug_log.h`

This keeps dependencies visible and makes it easier to continue shrinking
`app.h`.
