---
type: design
status: current
summary: How tag I2C controllers select hardware or software backends through descriptors, the pin policy, and how the RTC binds to a bus.
---

# I2C Backend Model

This note describes the common I2C backend model for tag firmware. It is not
specific to IMUTag: tags can use ChibiOS hardware I2C, the project-local
software I2C fallback, or both on different board-level buses.

## Goals

- Keep sensor and RTC chip drivers independent of the selected I2C backend.
- Allow production boards to use STM32 hardware I2C where pins and pullups are
  suitable.
- Allow development fixtures and breakout boards to use software I2C where
  flexible GPIO assignment is more useful than hardware peripheral support.
- Avoid compiling the ChibiOS fallback I2C driver unchanged beside hardware
  I2C, because both implementations use the same HAL symbols.
- Keep the backend choice in board/device descriptors.

## Backend Choice

Each `TagI2cController` describes one board-level I2C bus and names a fixed
backend:

```c
typedef enum {
  TAG_I2C_BACKEND_HARDWARE,
  TAG_I2C_BACKEND_SOFTWARE,
} TagI2cBackendKind;

typedef struct {
  TagI2cBackendKind backend;
  binary_semaphore_t *mutex;
  union {
    I2CDriver *hardware;
    TagSoftI2cDriver *software;
  } driver;
} TagI2cController;
```

`TagI2cDevice` carries the matching backend-specific config:

```c
typedef struct {
  const TagI2cController *controller;
  union {
    const I2CConfig *hardware;
    const TagSoftI2cConfig *software;
  } config;
  ioline_t sda;
  ioline_t scl;
  ioline_t pwr;
  uint8_t address;
  uint8_t alternate_function;
  uint32_t timeout;
  TagI2cSleepPolicy sleep_policy;
} TagI2cDevice;
```

The backend is fixed by the board descriptor. Normal runtime code should not
switch a single device between hardware and software I2C.

## Software I2C Copy

The ChibiOS fallback I2C driver is copied into the tag tree and renamed with
project-local symbols. The copied files retain the ChibiOS license header, but
do not export HAL LLD names such as `I2CDriver`, `I2CConfig`,
`I2CD1`, or `i2c_lld_master_transmit_timeout()`.

Targets that need ChibiOS hardware I2C must not set
`USE_HAL_I2C_FALLBACK = yes` in `project.mk`. That make switch replaces the
ChibiOS STM32 I2C LLD globally. Under this model, hardware-I2C targets use the
normal ChibiOS STM32 I2C LLD for `TAG_I2C_BACKEND_HARDWARE` and use
`TagSoftI2cDriver` for any software-I2C development buses.

Public names:

- `TagSoftI2cDriver`
- `TagSoftI2cConfig`
- `tagSoftI2cObjectInit()`
- `tagSoftI2cStart()`
- `tagSoftI2cStop()`
- `tagSoftI2cMasterTransmitTimeout()`
- `tagSoftI2cGetErrors()`

The copied driver keeps the fallback driver's bit-bang transaction logic, but
the common tag layer owns object lifetime, bus locking, pin mode selection, and
dispatch.

## Pin Policy

`tagI2cBusBegin()` applies the active pin mode required by the selected backend:

- hardware I2C: configure SDA/SCL as the board-selected STM32 alternate
  function, open-drain, with the selected pull policy;
- software I2C: configure SDA/SCL as GPIO open-drain and let the copied
  fallback backend toggle the configured lines.

`tagI2cDevicePowerOn()` and `tagI2cDevicePowerOff()` remain responsible for
optional switched device power and safe idle pin state. They avoid hard-coding
software-I2C GPIO modes that would break hardware-I2C devices.

There is no current need to support hardware and software I2C on the same
physical SDA/SCL pair. Software I2C is for different board-level buses,
bring-up fixtures, weak-pullup buses, or swapped-pin breakout boards.

## Register Transactions

Register-oriented drivers use:

- `tagI2cReadRegister()`
- `tagI2cWriteRegister()`
- `tagRegisterRead()`
- `tagRegisterWrite()`

`sensor_io.c` calls a backend-neutral common transfer helper instead of calling
`i2cMasterTransmitTimeout()` directly. The helper dispatches based on
`device->controller->backend`.

Transfer helper:

```c
msg_t tagI2cMasterTransmitTimeout(const TagI2cDevice *device,
                                  i2caddr_t address,
                                  const uint8_t *txbuf,
                                  size_t txbytes,
                                  uint8_t *rxbuf,
                                  size_t rxbytes,
                                  sysinterval_t timeout);
```

The hardware backend calls ChibiOS `i2cMasterTransmitTimeout()`. The software
backend calls `tagSoftI2cMasterTransmitTimeout()`.

## RTC Migration

The common power code no longer owns the default RTC I2C bus descriptor. The
weak RV3028 binding in `rtc_device.c` owns the default software-I2C controller,
config, and device descriptor, while `pwr.c` asks the selected `TagRtcDevice`
which register bus to power and open.

The RTC path follows these rules:

1. Keep RTC chip drivers descriptor-backed through `TagRtcDevice`.
2. Put the concrete RTC `TagI2cDevice` and backend config in tag or family
   device bindings, or in the weak common RV3028 default.
3. `rtcOn()` and `rtcOff()` operate on `tagRtcDevice()->registers` instead of
   on a private static `rtc_bus` owned by `pwr.c`.
4. Keep `pwr.c` responsible for power sequencing and standby entry, not for the
   type of I2C implementation used by the external RTC.
5. Let tags choose hardware or software RTC I2C in their descriptors. A
   development board with swapped pins can use software I2C; a production board
   with valid hardware-I2C pins can use the hardware backend.

After this migration, RV3028/RV3032/RV8803 register code remains unchanged. The
only code that should care about the backend is `i2c_bus.c` and the board-level
descriptor setup.

## Current Bindings

The IMUTag family overrides the weak RTC binding in
`families/IMUTag/src/devices.c`: the RV3028 sits on `I2CD1` through the
hardware backend, shared with the BMM350, with a board-supplied `reset` hook for
[I2C bus recovery](i2c-bus-recovery.md). Other tags use the weak common
RV3028 default in `rtc_device.c`. Remaining work on the descriptor model is in
[the tag TODO](../../../TODO.md).
