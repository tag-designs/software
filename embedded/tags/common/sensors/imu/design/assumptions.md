# LSM6DSV16X Driver Design Assumptions

This file captures the implemented design assumptions for the LSM6DSV16X IMU
driver. The longer conversation transcript in `design_notes.md` remains useful
as design history; this file describes the current driver contract.

## Ownership Boundary

The reusable driver lives in `embedded/tags/common/sensors/imu` and owns the
LSM6DSV16X register programming sequence. It does not know tag-local board line
names, MCU timer registers, chip-select macros, or global SPI helper functions.

The tag or family `devices.c` file owns the concrete hardware binding:

- the `TagRegisterDevice` used for register access;
- the optional trigger output line or timer binding used by ODR-triggered mode;
- INT1/INT2 board lines and wake-source policy;
- standby pin policy for the board;
- the self-test table entry and test context.

## Descriptor Model

Every public driver call receives a `const TagLsm6dsv16xDevice *`.

```c
typedef void (*TagLsm6dsv16xSetTrigger)(const void *context,
                                        unsigned int divider);

typedef struct {
  const TagRegisterDevice *registers;
  TagLsm6dsv16xSetTrigger set_trigger;
  const void *trigger_context;
} TagLsm6dsv16xDevice;
```

`registers` is required. `set_trigger` is optional unless MODE 3
ODR-triggered FIFO streaming is used. A descriptor without `set_trigger` may
still use shutdown, accelerometer-only mode, wakeup mode, direct accelerometer
reads, and accelerometer self-test.

Register IO stays on the existing common bus path:

```c
tagBusPowerOn(&device->registers->bus);
tagBusBegin(&device->registers->bus);
tagRegisterRead(device->registers, reg, buf, len);
tagRegisterWrite(device->registers, reg, buf, len);
tagBusEnd(&device->registers->bus);
tagBusPowerOff(&device->registers->bus);
```

Active-mode initializers leave the sensor powered so later reads can succeed.
Shutdown and self-test leave the sensor powered down.

## Public Modes

The public API exposes four mode initializers:

- `lsm6dsv16x_init_shutdown()` powers both sensors down, disables FIFO,
  clears interrupt routing, disables the ODR trigger, and powers the bus down.
- `lsm6dsv16x_init_accel_only()` configures high-performance accelerometer
  sampling from the internal ODR and routes data-ready to INT1.
- `lsm6dsv16x_init_accel_wakeup()` configures low-power accelerometer sampling
  with slope-filter wakeup and routes wakeup events to INT1.
- `lsm6dsv16x_init_accel_gyro_triggered()` configures accelerometer and gyro
  FIFO streaming from an external ODR trigger on INT2 and routes FIFO
  watermark plus sleep-change events to INT1.

Filter selection is intentionally not exposed. The driver applies the same
Nyquist-minimum policy in all modes: accelerometer LPF1 at ODR/2, accelerometer
LPF2 disabled, and gyro optional LPF disabled.

## Trigger Ownership

The MCU timer output is board/family behavior. The common driver computes the
required divisor, then calls:

```c
device->set_trigger(device->trigger_context, divider);
```

Passing divider `0` disables the trigger. Shutdown, accelerometer-only mode,
wakeup mode, and self-test disable the trigger. Triggered FIFO mode configures
the sensor first, then enables the MCU trigger at the derived divisor.

Application code should not call the trigger callback directly while using the
driver. If code needs to inspect the timer parameters, it can call:

```c
int lsm6dsv16x_get_trig_params(lsm6dsv16x_trig_odr_t odr,
                               lsm6dsv16x_trig_params_t *params);
```

## ODR-Triggered Mode

The MCU timer base is 1024 Hz, and its output frequency is `1024 / D`. The
LSM6DSV16X ODR-trigger count is `S`, where `S` may be an integer from 8 to
510. ST defines this with two-sample resolution, so the output sample rate is:

```text
f_out = 2S * (1024 / D)
```

The standard requested ODRs are exactly achievable with `S = 25`:

| Target ODR | MCU divisor D | MCU trigger | Multiplier S | Sensor output |
| --- | ---: | ---: | ---: | ---: |
| 50 Hz | 1024 | 1 Hz | 25 | 50 Hz |
| 100 Hz | 512 | 2 Hz | 25 | 100 Hz |
| 200 Hz | 256 | 4 Hz | 25 | 200 Hz |
| 400 Hz | 128 | 8 Hz | 25 | 400 Hz |
| 800 Hz | 64 | 16 Hz | 25 | 800 Hz |
| 1600 Hz | 32 | 32 Hz | 25 | 1600 Hz |

AN5763 requires the sensors to remain powered down for at least 500 us before
ODR-triggered configuration registers are written. The driver uses the existing
tag timekeeping helper and rounds this to `stopMilliseconds(1)`.

Mode 3 leaves the MCU trigger pin high-Z until the IMU has been configured to
use INT2 as the external trigger input. The IMU interrupt pins remain in their
normal push-pull configuration so INT1 can continue to act as the wake line.

## FIFO Policy

FIFO unpacking stays inside the common driver. Each FIFO word contains a tag
byte and six data bytes. The driver consumes all FIFO words and discards
unrelated tags.

For gyro and accelerometer words, tag byte bits `[7:3]` identify the sensor and
bits `[1:0]` identify a rotating time slot. The driver pairs gyro and accel
words by this time-slot counter rather than by FIFO arrival order, so either
sensor may arrive first within a slot. A counter change closes the previous
slot; if the previous slot is incomplete or otherwise inconsistent, the driver
returns a zeroed placeholder sample for that slot. Detectable counter jumps also
produce zeroed placeholders for the missing modulo-4 slots. This preserves the
sample timeline so host-side decoding can decide how to recover.

Samples are raw two's-complement values. Callers convert to physical units
using the active full-scale sensitivity.

## Environmental Samples

IMUTag data blocks carry one compact pressure sample and one compact
magnetometer sample alongside ten IMU accel/gyro samples. At high IMU rates the
environmental sensors are intentionally capped near 50 Hz and sampled sparsely:
800 Hz IMU records a fresh pressure/mag value every other block, and 1600 Hz
records a fresh value every fourth block. Blocks without a fresh environmental
sample store `-1` in the compact pressure field, or `-1, -1, -1` in the compact
magnetometer fields. The protobuf download keeps these compact raw values and
the host SQLite writer performs unit conversion while skipping sentinel rows.
The sentinel and compact-unit conversion constants live in the repository root
shared include file `include/imutag_log_format.h`.

## Self-Test

The common IMU module provides a descriptor-backed test hook:

```c
TestResult tag_test_lsm6dsv16x(const void *context);
```

The context is a `const TagLsm6dsv16xDevice *`. The hook runs the mode-1
accelerometer self-test path, returns `ALL_PASSED` on success, and returns
`AIS2_FAILED` on failure. This keeps the IMU test on the existing
accelerometer-family monitor result until a future protocol change adds a more
specific IMU result.

The low-level self-test routine:

- verifies `WHO_AM_I`;
- configures HP / 120 Hz / +/-4 g with LPF2 disabled;
- averages raw accelerometer samples before and after positive self-test;
- checks each axis against the 50-1700 mg datasheet limits;
- logs failure points through `debug_log_printf()` when `debug_log` is built;
- leaves the device in shutdown with the ODR trigger disabled.

The accelerometer self-test field is `CTRL10.ST_XL[1:0]`, the low two bits of
`CTRL10`. The gyro self-test field is separate and is not used by this driver
path.

## IMUTagBreakout Binding

`IMUTagBreakout` selects the `sensor_imu_lsm6dsv16x` module and exports
`TAG_IMU_DEVICE` from `embedded/tags/IMUTagBreakout/src/devices.c`.

The IMU shares the tag's normal register-device infrastructure. The descriptor
is the single source of truth for mixed or shared wiring, so the common driver
does not require a dedicated set of IMU-specific board-line names.
