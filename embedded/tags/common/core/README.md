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
- `device.c`: generic table walker for tag/family device descriptors. It lets
  `pwr.c` ask each converted device to prepare for standby without knowing the
  device type.
- `bus_power.c`: board-line descriptor helpers for SPI/I2C device power,
  bus-session setup, standby pulls, and Stop2 bus suspend/resume orchestration.
- `spi_bus.c`, `i2c_bus.c`, `usart_bus.c`: low-level byte/register transfers
  that are useful outside a single sensor family. SPI and USART also own their
  controller setup, active-state tracking, and Stop2 suspend/resume mechanics.
- `debug_log.c`: optional monitor-readable debug-message buffer selected by
  the `debug_log` module.

## Power And Bus Ownership

The standby path has two layers:

- `TagDevice.prepare_standby`: protocol-level work before standby. For example,
  external flash may be woken briefly and then commanded into its chip-level
  sleep mode for final states.
- `TagDevice.apply_standby_pins`: MCU pin policy before standby. This should
  only program standby pullup/pulldown state for the device pins. The lower
  bus helpers, such as `tagSpiDevicePrepareSleep()`, translate a device
  descriptor's sleep policy into the actual PWR pull registers.

Tag or family `devices.c` files provide `tagDeviceTable[]`; older tags can
omit the table and use the weak empty default while the migration continues.

Power lifetime and bus lifetime are intentionally separate. For SPI devices:

- `tagSpiDevicePowerOn/Off()` handles optional switched device power and safe
  pin states.
- `tagSpiBusBegin/End()` handles alternate functions and delegates mutex plus
  SPI controller enable/disable to the shared controller descriptor. The
  device descriptor supplies the `TagSpiConfig` used for that bus session.
- `tagSpiDevicePrepareSleep()` applies standby pull policy before deep sleep.

USART-backed sensor buses follow the same split: the device-side
`TagUsartBus` descriptor supplies a `TagUsartSyncConfig`, while `usart_bus.c`
owns the USART2 register setup and stop/resume mechanics.

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
