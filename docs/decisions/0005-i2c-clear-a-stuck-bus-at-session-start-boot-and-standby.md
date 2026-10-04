---
type: decision
status: accepted
summary: STM32U3 IMUTag targets clear a stuck I2C bus at session start, at boot and at standby entry, behind TAG_I2C_BUS_CLEAR, because a monitor attach can reset the core mid-byte.
---

# 0005. Clear a stuck I2C bus at session start, boot and standby entry

Date: 2026-09-03 (commit 9366a318); bus-end clear removed 2026-09-04
(commit 24c1f867).

Cut verbatim from [I2C Bus Recovery](../../embedded/tags/common/core/design/i2c-bus-recovery.md), which keeps the design,
the call sites and the pin-mode rules.

## Context

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

## Decision

Enable `tagI2cBusClearIfStuck()` through `TAG_I2C_BUS_CLEAR` for the STM32U3
IMUTag targets; see [I2C Bus Recovery](../../embedded/tags/common/core/design/i2c-bus-recovery.md), "Design".

## Evidence

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

## Consequences

A clear must not be placed at bus end, and must leave the pins released
open-drain; see [I2C Bus Recovery](../../embedded/tags/common/core/design/i2c-bus-recovery.md),
"Where a clear must not go" and "The pin-mode trap".
