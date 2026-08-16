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

## U375 Idle Hooks

`IMUTagU3bmm350` and `IMUTagNand` apply `idlePowerMode`.

Their `chconf.h` files wire the ChibiOS idle hooks to:

- `idle_enter()`
- `idle_loop()`
- `idle_leave()`

`idle_loop()` is deliberately small. If the firmware monitor is attached or
trying to attach, it returns without executing WFI. Otherwise it delegates the
selected mode to `tagPowerEnterIdleMode()`, the STM32U3 returned-idle helper in
`common/core/src/pwr-u375.c`.

The helper keeps the same monitor guard defensively, then owns the
silicon-specific sequence:

- `SLEEP`: clear `SCB->SCR.SLEEPDEEP`;
- `STOP0`: set `PWR->CR1.LPMS = 0`, set `SLEEPDEEP`, execute WFI, then clear
  `SLEEPDEEP`;
- `STOP1`: set `PWR->CR1.LPMS = LPMS_0`, set `SLEEPDEEP`, execute WFI, then
  clear `SLEEPDEEP`;
- `STOP2`: set `PWR->CR1.LPMS = LPMS_1`, set `SLEEPDEEP`, execute WFI, then
  clear `SLEEPDEEP`.

`idle_enter()` and `idle_leave()` are empty for these U375 targets. Returned
STOP wake does not restore VCORE range in the idle hook; internal flash
programming owns the temporary Range 1 requirement for flash writes.

`TAG_IDLE_STOP_DIAGNOSTICS` can pulse `LINE_LED1` from the common helper around
returned STOP entry. Leave it disabled for normal IMUTagNand current
measurements so PA1 and PA2 are not driven by the idle path.

## Normal Collection Idle

After each state-machine pass, the common main loop uses `godown(sleepmode)`
for terminal low-power requests. While the state is `TagState_RUNNING`, it then
sets the returned-idle selector around the blocking event wait:

```c
idlePowerMode = TAG_RUNNING_IDLE_POWER_MODE;
wait_events = EVT_HARDWARE_ALL;
if (isMonitorEnabled())
    wait_events |= EVT_MONITOR_ALL;
pending_events = chEvtWaitAny(wait_events);
idlePowerMode = TAG_DEFAULT_IDLE_POWER_MODE;
```

The default common policy is `TAG_RUNNING_IDLE_POWER_MODE=STOP2` during the
RUNNING wait and `TAG_DEFAULT_IDLE_POWER_MODE=SLEEP` outside scoped runtime
waits.

STOP0 and STOP1 were both evaluated as default idle modes for `IMUTagNand`.
The measured power difference was negligible, while the extra STOP transitions
raised reliability concerns, so the target keeps Sleep as its default idle mode.

Only targets with idle hooks act on this. On `IMUTagU3bmm350` and
`IMUTagNand`, this means the idle thread can enter the selected returned STOP
mode while the main thread is blocked waiting for hardware events. Monitor
events are included in the wait set only when `isMonitorEnabled()` is already
true; in that case the idle hook returns without WFI so the monitor/debug path
stays responsive.

The wake model is event-driven. The IMU FIFO watermark and other hardware
events are expected to wake the main thread through the existing event path.

## Synchronous SPI Waits

Long SPI stream transfers can block in the ChibiOS driver while DMA or the SPI
peripheral finishes the transfer. External flash payload writes use STOP1;
selected SPI reads use STOP0.

### Flash Program Payload Writes

`tagStorageSpiBlockWrite()` in `storage_spi.h` brackets long flash payload
writes before calling `tagSpiWrite()`:

```c
saved_idle_power_mode = idlePowerMode;
idlePowerMode = STOP1;
ok = tagSpiWrite(device, buf, n);
idlePowerMode = saved_idle_power_mode;
```

Command, address, write-enable, and status-poll transactions remain on the
normal path. The STOP1 bracket covers only the bulk payload phase, such as
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
drivers for normal reads.

Small register transactions generally remain on the polled path and do not
change `idlePowerMode`.

## Internal STM32 Flash Writes

STM32U3 internal flash row programming requires VCORE Range 1. The U3 flash
write path checks the current VCORE state before programming a row, switches to
Range 1 when needed, performs the write, and restores the configured run range
when that configured range is Range 2.

This range change is deliberately local to `FLASH_Program_Row()`. STOP wake
recovery in the idle hook does not adjust VCORE just to satisfy later flash
writes.

Bench measurements on `IMUTagNand` found that the current surge before an
internal checkpoint write is caused by the STM32U3 VCORE range transition
rather than the flash row program itself. The Range 2 to Range 1 transition
cost is about 0.8 uJ when STOP1/STOP2 use leaves the core in the configured
lower-power run range before the next internal flash write. The checkpoint
cadence is fixed, so this energy should be treated as the required cost of
using STOP modes before internal flash checkpoints rather than something the
flash writer can amortize by reducing write frequency.

This measured cost is consistent with the energy needed to charge the local
VCORE/LDO capacitance during the range step. For example, charging a 4.7 uF
capacitor from 1.0 V to 1.2 V is
`0.5 * 4.7 uF * (1.2^2 - 1.0^2)`, or about 1.0 uJ before capacitor tolerance,
actual rail voltage, regulator, and measurement effects.

## What Is Not Implemented

The current design does not provide a ChibiOS system timer backed by LPTIM for
returned idle. The normal system timer is still the configured ChibiOS timer
for the target. Do not assume arbitrary ChibiOS timeouts continue to advance
while the core is in STOP0, STOP1, or STOP2.

There is also no general peripheral parking framework for runtime STOP entry.
The implemented runtime path relies on the owning drivers to open and close bus
sessions normally. Terminal standby still uses the device standby hooks.

Autonomous SPI/I2C/DMA operation through STOP modes is not treated as a shared
capability. The current STOP0 use is limited to allowing the idle thread to
sleep while synchronous SPI waits are blocked in the driver. The current STOP1
transfer use is limited to external flash payload writes.

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
- Uses STOP2 for ordinary blocked idle.
- Uses STOP1 around external flash payload writes.
- Uses STOP0 around selected SPI receive waits.

`IMUTagNand`:

- Shares the STM32U3 core support and build settings.
- Installs idle hooks.
- Applies `idlePowerMode` in `power_modes.c`.
- Uses STOP2 during the RUNNING event wait.
- Uses Sleep as the default idle mode outside scoped waits.
- Uses STOP1 around external flash payload writes.
- Uses STOP0 around selected SPI receive waits.
- Disables idle-hook diagnostic pin drives unless `TAG_IDLE_STOP_DIAGNOSTICS`
  is enabled.

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
- `IMUTagNand`

Hardware validation should focus on:

- monitor attach, especially early `TAG_MONITORINFO` calls;
- U375 returned STOP2 entry and wake from hardware events;
- flash writes with STOP1 around the payload transfer;
- current draw with debug test lines removed or disabled.
