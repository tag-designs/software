---
type: design
status: current
summary: Why a reset mid-transaction wedges the shared I2C bus, and the bus-clear recovery, its call sites and pin-mode rules on STM32U3 IMUTag targets.
last-verified: a87fc84a
---

# I2C Bus Recovery

IMUTagNand and IMUTagNandBmp581 clear a stuck I2C bus at three points: at the start of
every bus session, at boot before the first RTC read, and at standby entry.
The clear acts only when SDA reads low. It is compiled in by
`TAG_I2C_BUS_CLEAR=1` in those targets' `project.mk` and is off everywhere
else. Why it exists, the evidence, and the hardware verification are in
[0005](../../../../../docs/decisions/0005-i2c-clear-a-stuck-bus-at-session-start-boot-and-standby.md).

## The failure

A core reset in the middle of an I2C byte leaves the addressed slave driving
SDA low, waiting for clocks that never arrive. The master cannot issue a START
while the bus is not idle, so **every device on that controller fails for the
rest of the boot**. Nothing recovers it, because recovery needs the bus.

A monitor attach causes exactly this reset, routinely: the host connects under
reset, and it can land mid-transaction.

On IMUTagNandBmp581 the RV-3028 and the BMM350 share one controller and one
pair of pins -- `tagRtcI2cController` on `I2CD1`, `LINE_RTC_SDA` and
`LINE_RTC_SCL` -- so a wedged bus takes out both the clock and the
magnetometer together. That is what made two apparently unrelated faults one
fault:

- `tag-start --set-rtc` failing with "RTC sync failed", ~13% of attempts;
- collection aborting at start or on reattach, ~1 in 3 attach events.

Both faults were the bus; see [0005](../../../../../docs/decisions/0005-i2c-clear-a-stuck-bus-at-session-start-boot-and-standby.md) for the evidence.

## Design

One function in `core/src/i2c_bus.c` owns the decision and both backends:

```c
bool tagI2cBusClearIfStuck(const TagI2cDevice *device);
```

- **Acts only when SDA reads low.** A healthy bus is never touched, which is
  what makes it safe to enable broadly.
- **Never drives an unpowered part.** Returns early when the device has a
  switched power line that is currently deasserted; `tagI2cDevicePowerOff()`
  parks SDA and SCL as analog inputs for the same reason, and clocking a
  device whose supply is down injects current through its protection diodes.
  Latent on the current board -- both devices are permanently powered -- which
  is precisely why the guard belongs in the shared function and not at the call
  sites.
- **Hardware backend:** disable the controller, take the pins as open-drain
  outputs, up to nine SCL pulses while SDA stays low, a STOP, then the
  controller's `reset` hook before handing the pins back.
- **Software backend:** delegate to the existing `tagSoftI2cBusClear()`.

The peripheral reset is a `void (*reset)(void)` member on `TagI2cController`,
supplied by board code, rather than an `I2CD1` comparison inside the shared
layer. Clearing the wire is not sufficient on its own: the peripheral latches
BUSY from the bus and can stay stuck regardless of what the pins then read.

### Call sites

`tagI2cBusBegin()` is the session choke point that can host a clear -- nothing
outside `i2c_bus.c` calls `tagI2cControllerEnable()`, so every transaction
passes through it.

A clear may only be placed where something restores the pin mode afterwards.
`tagI2cBusClearHardware()` drives SDA and SCL directly and therefore leaves
them as plain open-drain GPIO outputs; only `tagI2cBusBegin()` follows it with
`tagI2cApplyActivePins()`. This is not a detail -- see **Where a clear must
not go** below.

| site | placement | why |
| --- | --- | --- |
| `tagI2cBusBegin()` | before the controller is enabled, and before the active pin mode is applied | a controller started against a non-idle bus latches BUSY |
| startup | from IMUTag `devices.c` device init, before the first RTC read | the boot-time external RTC query runs early; clearing only inside `tagI2cBusBegin()` would let that first read fail before recovery could run |
| standby entry | `tagDevicesApplyPowerState(TAG_DEVICE_POWER_STANDBY_ENTRY, ...)` | leaves the bus idle, so a slave is not left holding SDA against the standby pull-ups, and the next boot does not inherit a wedged bus |

### Where a clear must not go

**Not in `tagI2cBusEnd()`**, the natural-looking partner to
`tagI2cBusBegin()`. The clear leaves the pins as open-drain GPIO outputs, and
nothing at bus end restores them to alternate function. A transaction that
ends with a slave holding SDA, which is exactly when the clear fires, therefore
leaves the pins as GPIO against the board's 4.7k pull-ups into the terminal
sleep. Setting the clock is normally the last preparation step, so a prepared
tag would sit at about 1 mA instead of about 5 uA while reporting IDLE. That is
roughly 11 hours of battery life rather than 100 days. The fault presents as an
RTC fault rather than a pin-state fault: writes trip the clear and reads do
not, so it isolates to `rv3028SetDateTime()`. A clear at bus end was tried and
removed in commit 24c1f867.

Nothing is lost by leaving bus end alone. `tagI2cBusBegin()` clears before
enabling the controller, so the next user of the bus recovers it, and the
startup hook clears at boot, so a slave left holding SDA across standby is
recovered on the next startup.

**Not in `tagI2cDevicePrepareSleep()`**, the obvious-looking home for the
standby clear. Both U375 targets set `TAG_STANDBY_PULLS_CONFIGURED_BY_MCUCONF`,
so `tagDevicesApplyStandbyPins()`, its only caller, never runs.
`tagDevicesApplyPowerState()` is called unconditionally from the terminal
sleep path (see [STM32U375 Low Power](u375-low-power.md#terminal-sleep-stop-3)).

## The pin-mode trap

The clear must leave the pins as **released open-drain**, never in alternate
function. Only `tagI2cBusBegin()` follows it with a controller start. At the
other sites the peripheral stays disabled, and an AF pin with no peripheral
driving it is held low. The board pulls SCL and SDA up with 4.7k, so a line
parked low sinks about 700 uA. A version that ended with `tagI2cApplyActivePins()`
measured 1031 uA at idle against 4.09 uA
([0005](../../../../../docs/decisions/0005-i2c-clear-a-stuck-bus-at-session-start-boot-and-standby.md)).

## Scope

Enabled by `TAG_I2C_BUS_CLEAR=1`, set through `UDEFS` in the target
`project.mk`, not in `custom.h` -- `i2c_bus.h` applies its own default and does
not include `custom.h`, so a header define would depend on include order.

With the macro off, the declaration, the definition, the call sites and the
controller's `reset` member are all compiled out, so other targets' images are
unchanged. A non-static function has external linkage and is emitted even when
nothing calls it, which is why every one of them is guarded.
