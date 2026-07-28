# STM32U375 Stop-Mode Support

This note describes the current U375 stop-mode design as implemented in the
tag firmware. It is not a future work plan.

The design has two separate low-power paths:

- Terminal standby entry through `godown(STANDBY)`.
- Runtime idle stop/sleep selection through the shared `idlePowerMode` state.

Only targets with idle power-management hooks use `idlePowerMode` in practice.
Targets without those hooks still build with the shared variable but ignore it.

## Terminal Standby

`godown()` in `embedded/tags/common/core/src/pwr.c` is intentionally a pure
standby function. It returns unless both of these conditions are true:

- the requested sleep mode is `STANDBY`;
- the monitor is not active or trying to attach.

The monitor guard is `isMonitorEnabled()`. That predicate is deliberately
broader than `monitorIsAttached()`:

```c
return MONCONNECTED || monitorIsAttached();
```

This matters on the STM32L4 monitor path. During the early attach/info phase,
the host has set `VC_CORERESET` (`MONCONNECTED`) but has not yet completed
`MONITORSTART`, so `monitorIsAttached()` is still false. Treating
`MONCONNECTED` as monitor-enabled prevents L4 targets such as PresTag from
entering standby while the host is fetching monitor metadata.

`godown()` no longer contains STOP0, STOP1, STOP2, or STM32U3-specific idle
code. It prepares devices and standby pin pulls, configures wake sources,
selects standby in `PWR->CR1`, sets `SLEEPDEEP`, and executes `WFI`.

## Runtime Idle Power Mode

The shared runtime selector is:

```c
volatile enum Sleep idlePowerMode = SLEEP;
```

It is declared in `core_runtime.h` and defined in `state_machine.c`. Valid
runtime idle values are:

- `SLEEP`
- `STOP0`
- `STOP1`
- `STOP2`

`STOP0` was added to the existing `enum Sleep` so the same enum can carry both
state-machine sleep requests and runtime idle power requests. `godown()` only
acts on `STANDBY`; the stop values are consumed by target-specific idle hooks.

## U3bmm350 Idle Hooks

`IMUTagU3bmm350` is the current target that applies `idlePowerMode`.

Its `chconf.h` wires the ChibiOS idle hooks to:

- `idle_enter()`
- `idle_loop()`
- `idle_leave()`

`idle_enter()` reads `idlePowerMode` and configures the core for the requested
idle mode:

- `SLEEP`: clear `SCB->SCR.SLEEPDEEP`;
- `STOP0`: set `PWR->CR1.LPMS = 0` and set `SLEEPDEEP`;
- `STOP1`: set `PWR->CR1.LPMS = LPMS_0` and set `SLEEPDEEP`;
- `STOP2`: set `PWR->CR1.LPMS = LPMS_1` and set `SLEEPDEEP`.

`idle_loop()` is deliberately small: if the firmware monitor is attached it
avoids deep sleep; otherwise it executes the WFI sequence. Mode selection does
not live in `idle_loop()`.

`idle_leave()` clears the debug test lines and clears `SLEEPDEEP`.

`LINE_LED1` is configured as analog by the board file, so the debug pulse in
the idle power hook switches it to output push-pull before driving it high and
returns it to analog on idle leave. `LINE_testpin` is already an output and is
used to show that the idle loop reached WFI.

## Normal Collection Idle

The common main loop sets:

```c
idlePowerMode = STOP1;
pending_events = chEvtWaitAny(EVT_MONITOR_ALL | EVT_HARDWARE_ALL);
idlePowerMode = SLEEP;
```

Only targets with idle hooks act on this. On `IMUTagU3bmm350`, this means the
idle thread can enter STOP1 while the main thread is blocked waiting for
monitor or hardware events.

The wake model is event-driven. The IMU FIFO watermark and other hardware
events are expected to wake the main thread through the existing event path.

## Short DMA/Driver Waits

Some SPI transfers are short enough that going all the way to STOP1 around the
blocking wait is not useful or safe during bring-up. The current design uses
STOP0 as the temporary idle mode around selected synchronous SPI driver waits.

### Flash Program Payload Writes

`tagStorageSpiBlockWrite()` in `storage_spi.h` brackets long flash payload
writes:

```c
saved_idle_power_mode = idlePowerMode;
idlePowerMode = STOP0;
ok = tagSpiWrite(device, buf, n);
idlePowerMode = saved_idle_power_mode;
```

Command, address, write-enable, and status-poll transactions remain on the
normal path. The STOP0 bracket covers only the bulk payload phase, such as
MX25U page-program data or GD5F program-cache data.

### SPI Reads

The ChibiOS SPI backend brackets `spiReceive()` in `tagSpiRead()`:

```c
saved_idle_power_mode = idlePowerMode;
idlePowerMode = STOP0;
result = spiReceive(driver, len, buf);
idlePowerMode = saved_idle_power_mode;
```

This places the policy at the SPI driver level instead of in individual sensor
drivers. The IMU FIFO read uses the ordinary register-block path; when that
path reaches `tagSpiRead()`, the receive wait can idle in STOP0.

Small register transactions generally remain on the polled path and do not
change `idlePowerMode`.

## What Is Not Implemented

The current design does not provide a ChibiOS system timer backed by LPTIM.
The normal system timer is still the configured ChibiOS timer for the target.
Do not assume arbitrary ChibiOS timeouts continue to advance while the core is
in STOP1.

There is also no general peripheral parking framework for runtime STOP entry.
The implemented runtime path relies on the owning drivers to open and close bus
sessions normally. Terminal standby still uses the device standby hooks.

Autonomous SPI/I2C/DMA operation through STOP modes is not treated as a shared
capability. The current STOP0 use is limited to allowing the idle thread to
sleep while synchronous SPI waits are blocked in the driver.

SRAM1 power-down is not part of this design.

## Debug And Monitor Behavior

Firmware monitor attachment must prevent terminal standby. The shared predicate
uses both:

- `MONCONNECTED`: host has asserted the vector-catch attach hint;
- `monitorIsAttached()`: target monitor session is active.

This preserves the STM32L4 attach sequence while supporting the STM32U3 shared
memory monitor path.

Debugger attachment alone is not the same as firmware monitor attachment.
During ordinary debugger use, `monitorIsAttached()` may be false. Debug pins or
logic-analyzer lines used in `power_modes.c` are only bring-up aids and are not
part of the low-power contract.

## Current Target Split

`IMUTagU3bmm350`:

- Uses the U375 board and STM32U3 runtime.
- Installs idle hooks.
- Applies `idlePowerMode` in `power_modes.c`.
- Uses STOP1 for ordinary blocked idle.
- Uses STOP0 around selected SPI driver waits.

`IMUTagU375`:

- Shares the STM32U3 core support and build settings.
- Does not currently install the same `power_modes.c` idle hook implementation.
- Therefore sees the shared `idlePowerMode` variable but does not act on it
  unless equivalent hooks are added.

STM32L4 targets such as `PresTag`:

- Ignore `idlePowerMode` unless they add their own idle hooks.
- Continue to use `godown(STANDBY)` for terminal standby.
- Depend on `MONCONNECTED || monitorIsAttached()` to keep standby out of the
  monitor attach path.

## Validation Notes

The implementation has been build-checked on:

- `IMUTagU3bmm350`
- `IMUTagU375`
- `PresTag`

Hardware validation should focus on:

- PresTag monitor attach, especially early `TAG_MONITORINFO` calls;
- U3bmm350 idle STOP1 entry and wake from hardware events;
- flash writes with STOP0 around the payload transfer;
- LSM6DSV16X FIFO reads with STOP0 around `spiReceive()`;
- current draw with debug test lines removed or disabled.
