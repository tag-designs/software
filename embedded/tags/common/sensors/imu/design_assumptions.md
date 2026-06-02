# LSM6DSV16X Integration Design Assumptions

This document captures the final integration assumptions for bringing the
LSM6DSV16X IMU driver into the current tag device model. The conversation
transcript in `design_notes.md` is useful design history; this file is the
target shape for implementation.

## Driver Ownership

The reusable LSM6DSV16X register driver belongs in
`embedded/tags/common/sensors/imu`. It should not know about tag-local board
line names, MCU timer registers, ChibiOS semaphores, or global SPI helper
functions.

The tag or family `devices.c` file owns the concrete hardware binding:

- the `TagRegisterDevice` used for register access;
- the optional trigger output line or timer binding used by ODR-triggered mode;
- INT1/INT2 board lines and wake-source policy;
- standby pin policy for the board;
- the self-test table entry and test context.

## Descriptor Shape

The driver should use an explicit IMU descriptor rather than the BSP hooks in
the current draft header.

```c
typedef void (*TagLsm6dsv16xSetTrigger)(const void *context,
                                        unsigned int divider);

typedef struct {
  const TagRegisterDevice *registers;
  TagLsm6dsv16xSetTrigger set_trigger;
  const void *trigger_context;
} TagLsm6dsv16xDevice;
```

`registers` is required. `set_trigger` is optional for tags that never use
ODR-triggered FIFO mode; if absent, the triggered-mode initializer should fail
or no-op explicitly rather than silently pretending the trigger is configured.

This keeps normal register IO on the existing `TagRegisterDevice` path:

- `tagBusPowerOn(&device->registers->bus)`
- `tagBusBegin(&device->registers->bus)`
- `tagRegisterRead(device->registers, reg, buf, len)`
- `tagRegisterWrite(device->registers, reg, buf, len)`
- `tagBusEnd(&device->registers->bus)`
- `tagBusPowerOff(&device->registers->bus)`

## Public API Shape

Every public driver function should take a device descriptor pointer. Avoid
global default bindings and application-provided SPI/sleep hooks.

Examples:

```c
int lsm6dsv16x_verify_id(const TagLsm6dsv16xDevice *device);
void lsm6dsv16x_init_shutdown(const TagLsm6dsv16xDevice *device);
void lsm6dsv16x_init_accel_only(const TagLsm6dsv16xDevice *device,
                                const lsm6dsv16x_accel_cfg_t *cfg);
void lsm6dsv16x_init_accel_wakeup(const TagLsm6dsv16xDevice *device,
                                  const lsm6dsv16x_wakeup_mode_cfg_t *cfg,
                                  const lsm6dsv16x_motion_cfg_t *motion);
void lsm6dsv16x_init_accel_gyro_triggered(
    const TagLsm6dsv16xDevice *device,
    const lsm6dsv16x_trig_mode_cfg_t *cfg,
    const lsm6dsv16x_motion_cfg_t *motion);
```

Sleep calls should use the existing tag timekeeping helpers. Use
`stopMilliseconds()` rather than declaring BSP hooks in the IMU header; the
500 us ODR-triggered-mode delay is rounded up to `stopMilliseconds(1)`.

## Trigger Ownership

The MCU timer output is board/family behavior. The common IMU driver computes
the required divisor, then calls the descriptor's trigger callback:

```c
device->set_trigger(device->trigger_context, divider);
```

Passing divider `0` disables the trigger. Shutdown, accelerometer-only mode,
and accelerometer wakeup mode should disable the trigger. Triggered FIFO mode
should configure the LSM6DSV16X registers first, then enable the MCU trigger at
the derived divisor.

The application should not call the trigger callback directly while using this
driver. If application code needs to inspect the timer parameters, it should
call an informational query function:

```c
int lsm6dsv16x_get_trig_params(lsm6dsv16x_trig_odr_t odr,
                               lsm6dsv16x_trig_params_t *params);
```

## ODR-Triggered Mode Assumptions

The MCU timer base is 1024 Hz, and its output frequency is `1024 / D`. The
LSM6DSV16X internal multiplier is `S`, where `S` may be an integer from 8 to
510. The output sample rate is:

```text
f_out = S * (1024 / D)
```

The standard requested ODRs are exactly achievable with `S = 25`:

| Target ODR | MCU divisor D | MCU trigger | Multiplier S | Sensor output |
| --- | ---: | ---: | ---: | ---: |
| 50 Hz | 512 | 2 Hz | 25 | 50 Hz |
| 100 Hz | 256 | 4 Hz | 25 | 100 Hz |
| 200 Hz | 128 | 8 Hz | 25 | 200 Hz |
| 400 Hz | 64 | 16 Hz | 25 | 400 Hz |
| 800 Hz | 32 | 32 Hz | 25 | 800 Hz |

The query table should also carry the internal gate ODR code and FIFO batch
data-rate code required by the register programming sequence.

## Register And FIFO Policy

The register-level implementation from the draft should be preserved, but its
read/write helpers should dispatch through `TagRegisterDevice`.

The default filtering policy remains:

- accelerometer LPF1 at the inherent ODR/2 path;
- accelerometer LPF2 disabled;
- gyroscope optional LPF disabled;
- high-performance sampling for mode 1 and triggered mode;
- low-power slope-filter sampling for wakeup mode.

FIFO unpacking should remain inside the common driver. It should read tagged
FIFO words, discard unrelated tags, pair gyro and accelerometer words, and
return raw `lsm6dsv16x_sample_t` values.

## Self-Test

The common IMU module should provide a descriptor-backed self-test hook:

```c
TestResult tag_test_lsm6dsv16x(const void *context);
```

The context is a `const TagLsm6dsv16xDevice *`. The hook should run the mode-1
accelerometer self-test path, return `ALL_PASSED` on success, and return
`AIS2_FAILED` on failure. This keeps the IMU test on the existing
accelerometer-family monitor result until a future protocol change adds a more
specific IMU result.

The low-level self-test routine should leave the device in shutdown and disable
the ODR trigger before returning. Application code should reinitialize the
desired mode after a self-test.

## Build Module

Add a common module fragment:

```make
sensor_imu_lsm6dsv16x.mk
```

It should:

- include the common sensor paths;
- define `TAG_SENSOR_IMU_LSM6DSV16X=1`;
- include `sensor_io.c` only once using the existing guard pattern;
- add `lsm6dsv16x.c`;
- add the descriptor-backed self-test source once it exists.

The owning tag's `project.mk` selects the module. The owning tag or family
`devices.c` exports the descriptor and adds the test table entry.

## IMUTagBreakout Binding Assumption

For `IMUTagBreakout`, bind the IMU in `embedded/tags/IMUTagBreakout/src/devices.c`
using the standardized `IMUTagv1` board names generated from
`board-customizations.json`.

The driver should not require the IMU board to expose a dedicated `LINE_IMU_*`
set if the same physical SPI pins are already named for the accelerometer or
another board-level role. The descriptor in `devices.c` is the single source of
truth for mixed/shared wiring.

## Migration Checklist

1. Replace the BSP-hook API in `lsm6dsv16x.h` with descriptor-backed calls.
2. Move register helpers in `lsm6dsv16x.c` to `tagRegisterRead()` and
   `tagRegisterWrite()`.
3. Replace `sleep_ms()` and `sleep_us()` with tag timekeeping helpers.
4. Add `TagLsm6dsv16xDevice` and trigger callback types.
5. Add the trigger-parameter query function and keep the exact ODR table.
6. Add the common module fragment and update `BUILD_SOURCES.md`.
7. Add the descriptor-backed self-test hook.
8. Bind `IMUTagBreakout` in `devices.c` and add its test table entry.
9. Build `IMUTagBreakout` and any other tag that selects the IMU module.
