# Magnetometer Drivers

This directory contains reusable magnetometer drivers and their monitor
self-test hooks.

## Files

- `inc/ak09940a.h`, `src/ak09940a.c`: descriptor-backed AK09940A SPI register
  driver.
- `src/ak09940a_shim.c`: weak/default AK09940A binding and legacy `mag*` API
  shim for targets that still use generic magnetometer calls.
- `src/ak09940a_test.c`: AK09940A self-test hook.
- `inc/bmm350_tag.h`, `src/bmm350_tag.c`: native BMM350 I2C driver for tag
  firmware.
- `src/bmm350_test.c`: BMM350 self-test hook.

## BMM350 Maintenance Notes

The BMM350 driver is intentionally a small repo-native wrapper rather than a
compiled copy of the Bosch API. Bosch's SensorAPI is still the reference for
OTP compensation decoding, power-mode sequencing, interrupt setup, and
floating-point compensation math; copied/adapted logic must keep the attribution
comment in `bmm350_tag.c`.

`TagBmm350Device` is a dedicated descriptor instead of a `TagRegisterDevice`
because the driver needs:

- a `TagI2cDevice` register bus;
- a DRDY/INT line;
- RAM-owned factory compensation storage;
- BMM350 interrupt polarity and drive-mode settings.

Callers should bracket public configuration and sample operations with
`bmm350DeviceBegin()` / `bmm350DeviceEnd()`. Those helpers power the optional
device rail and open the I2C bus session; individual register helpers assume
the session is already open.

`bmm350InitContinuous()` reloads factory compensation data from OTP each time
it initializes the sensor. `bmm350ReadMagUT()` requires a valid compensation
buffer and returns compensated microtesla values. Do not store BMM350 factory
trim data in tag calibration flash; user calibration constants are a separate
layer.

Normal sampling should use the wired DRDY/INT line through
`bmm350DataReady()`. The driver configures latched data-ready interrupts and
clears the latch by reading `INT_STATUS` after a coherent data burst. If a tag
uses a different active level or drive mode, change the tag's
`TagBmm350Device` descriptor rather than branching in the driver.

## Current User

`IMUTagbmm350` binds BMM350 on the same swapped software-I2C bus as the RV3028
RTC and uses PA4 / `LINE_IMU_TRG_TEST` as the INT/DRDY input. The monitor test
table still maps this to the legacy `RUN_MMC5633` request until the protobuf
test enum grows a BMM350-specific request/result.
