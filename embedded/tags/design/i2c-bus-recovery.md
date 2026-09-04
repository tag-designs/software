# I2C Bus Recovery

Status: implemented for STM32U3 IMUTag targets, validated on hardware, not yet
committed. Off by default elsewhere.

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

## Evidence

In order, each step narrowing the previous one:

1. The `SetRtc` failure was isolated to `tagRtcApplyClockCorrection()` -- the
   RV-3028 EEOffset read -- not the date write. That also explains
   `ppm_clock_error: 0`, which the accessor documents as meaning the
   correction "has not been read successfully in this boot". The reported zero
   was a symptom, not a calibration value.
2. Retrying the read did not help: 3 failures in 24 attempts against 13.4%
   before. It fails **persistently within a boot**, so not a transient hiccup.
3. Probing the magnetometer at the moment of an RTC failure reported
   `mag FAILS TOO`. Both devices on the controller were unreachable together,
   which moves the fault from either part to the bus.
4. Sampling the lines at the moment of failure gave `sda0 scl1` in every
   capture: a slave holding SDA down with SCL released. That is the textbook
   wedged-bus signature.
5. The collection abort was the BMM350 **whoami** -- its first bus access, not
   a configuration step -- and it failed all five retries across ~10 ms, so the
   device was not slow to answer, it was unreachable.

## The recovery already existed, disabled

`tagI2cBusBegin()` called `tagSoftI2cBusClear()`, but only when
`controller->backend` was `TAG_I2C_BACKEND_SOFTWARE`, and only when
`TAG_I2C_SOFTWARE_BUS_CLEAR_ON_BEGIN` was set -- which defaulted to `0`. The
IMUTag targets use the hardware backend, so they had no recovery at all.

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

`tagI2cBusEnd()` looks like the natural partner to `tagI2cBusBegin()`, and a
clear was placed there originally. It cost 1 mA at idle and was removed.

The clear leaves the pins as open-drain GPIO outputs, and at bus end nothing
restores them to alternate function. A transaction that ends with a slave
holding SDA -- which is precisely when the clear fires -- therefore parked the
pins as GPIO against the board's 4.7k pull-ups, and they stayed that way into
standby. Setting the clock is normally the last preparation step, so a prepared
tag sat at about 1 mA instead of 5 uA until its next reset while still
reporting IDLE: roughly 11 hours of battery life rather than 100 days.

It presented as an RTC fault rather than a pin-state fault. The clear only runs
when SDA reads low, so writes tripped it and reads never did; the trigger
isolated to `rv3028SetDateTime()`, and running to finished and back to idle was
always clean because that path never sets the clock.

Nothing is lost by leaving bus end alone. `tagI2cBusBegin()` clears before
enabling the controller, so the next user of the bus recovers it, and the
startup hook clears at boot, so a slave left holding SDA across standby is
recovered on the next startup.

The standby hook must **not** go in `tagI2cDevicePrepareSleep()`, which is the
obvious-looking home: this target sets
`TAG_STANDBY_PULLS_CONFIGURED_BY_MCUCONF`, so `tagDevicesApplyStandbyPins()` --
its only caller -- never runs. `tagDevicesApplyPowerState()` is called
unconditionally from both U3 terminal paths.

## The pin-mode trap

The clear must leave the pins as **released open-drain**, never in alternate
function. Only `tagI2cBusBegin()` follows it with a controller start; at the
other three sites the peripheral stays disabled, and an AF pin with no
peripheral driving it is held low. The board pulls SCL and SDA up with 4.7k, so
a line parked low sinks about 700 uA. An earlier version of this change ended
with `tagI2cApplyActivePins()` and measured **1031 uA at idle against
4.09 uA** for exactly that reason.

## Verification

| | cycles | attach storms | RTC failures | start aborts |
| --- | ---: | ---: | ---: | ---: |
| before | -- | -- | 13.4% of attempts | ~1 in 3 attach events |
| after | 88 | 49 | **0** | **0** |
| after (longer run) | 154 | 155 | **0** | **0** |

Idle unaffected: 4.0736 uA with the full sequence enabled, against 4.09 uA with
it compiled out.

`UIUCTag` and `PresTag` `.list` output is byte-identical to HEAD. That required
guarding the declaration, the definition and every call site: a non-static
function has external linkage and is emitted even when nothing calls it, and
the `reset` member enlarges `TagI2cController` for every target that has one.

## Scope

Enabled by `TAG_I2C_BUS_CLEAR`, set through `UDEFS` in the target
`project.mk`, not in `custom.h` -- `i2c_bus.h` applies its own default and does
not include `custom.h`, so a header define would depend on include order.
