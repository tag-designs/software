# BMM350 And GD5F1GQ5RE Driver Plan

This note focuses only on the two new devices planned for `IMUTag`:

- Bosch BMM350 magnetometer.
- GigaDevice GD5F1GQ5RE-family SPI NAND flash.

The purpose is to define the driver boundaries before implementation. Target
creation, logger changes, host changes, and board wiring are intentionally out
of scope except where they affect the device-facing APIs.

## BMM350 Magnetometer

### Goal

Provide a concise BMM350 driver that fits the existing tag sensor model instead
of exposing the Bosch factory API directly to family code.

The driver should:

- use the existing `TagI2cDevice` bus/session/power model;
- use ChibiOS delay primitives, including `stopMilliseconds()` where useful;
- use floating-point compensation only;
- expose a small tag-facing API for identity, configuration, data-ready, sample
  readout, and power-down;
- use the BMM350 INT/DRDY pin for normal sample readiness instead of querying
  the device status register before each sample;
- refresh factory compensation data during driver initialization and store it
  in RAM through a pointer carried by the BMM350 device descriptor;
- use Bosch's factory API as a reference for register sequences and
  compensation math, but implement a repo-native driver.

### Driver Source Decision

Implement `bmm350_tag.c/.h` as a native tag driver rather than importing the
Bosch factory API as a compiled dependency. The Bosch API and datasheet remain
the reference for:

- OTP compensation read sequence;
- floating-point compensation math;
- valid ODR and averaging combinations;
- interrupt configuration;
- burst-read requirements;
- power-mode and high-field recovery behavior.

The native driver should copy/adapt only the required register definitions,
sequences, and math, with Bosch license/attribution preserved for any code
taken from the factory API. Do not carry over COINES examples, I3C support,
fixed-point conversion, generic callback plumbing, or portability layers unless
a future requirement needs them.

If compensation math later proves too large or fragile to maintain locally, the
fallback option is to isolate the Bosch source behind the same public
`bmm350_tag.h` API. That should be treated as a later implementation change,
not the default design.

### Proposed Files

```text
embedded/tags/common/sensors/mag/inc/bmm350_tag.h
embedded/tags/common/sensors/mag/src/bmm350_tag.c
embedded/tags/common/sensors/mag/src/bmm350_test.c
embedded/tags/common/modules/sensor_mag_bmm350.mk
```

### Descriptor

Use a dedicated descriptor rather than a raw `TagRegisterDevice`, because the
driver needs a DRDY line, runtime compensation storage, and BMM350-specific
configuration state.

```c
typedef struct {
  const TagI2cDevice *i2c;
  ioline_t drdy;
  TagBmm350Compensation *compensation;
} TagBmm350Device;
```

The `compensation` pointer is part of the device binding because BMM350 sample
conversion depends on factory trim data. It should point to RAM owned by the
tag or calibration state, not to flash-backed persistent storage. Normal tag
startup, collection entry, and calibration entry can all call the BMM350 init
path, which refreshes this RAM copy from the sensor.

### Bus Access

The native driver should access BMM350 registers directly through the repo's I2C
device model:

```text
BMM350 register read:
  tagI2cBusBegin(device->i2c)
  tagI2cReadRegister(device->i2c, reg, buf, len)
  tagI2cBusEnd(device->i2c)

BMM350 register write:
  tagI2cBusBegin(device->i2c)
  tagI2cWriteRegister(device->i2c, reg, buf, len)
  tagI2cBusEnd(device->i2c)
```

Power sequencing remains outside individual register accesses. The normal
driver entry points should call `bmm350DeviceBegin()` and `bmm350DeviceEnd()`
or require the caller to bracket a session explicitly. Prefer the same style as
the AK09940A driver: simple public helpers plus explicit device begin/end.

For this tag, the BMM350 should use the hardware ChibiOS I2C driver rather
than the software/fallback I2C path. This should be mostly transparent to the
BMM350 wrapper because it goes through `TagI2cDevice`, but it is an integration
constraint for the target: shared I2C bus ownership, timing configuration, and
startup/stop behavior need to match the hardware driver.

The main expected knock-on effect is RTC support. Any RV3028/RTC code that
currently depends on the fallback I2C implementation, delay behavior, or bus
recovery assumptions should be reviewed when the target switches to hardware
I2C.

### Delay Policy

Use ChibiOS primitives instead of portable busy-wait hooks:

- sub-millisecond waits: `chThdSleepMicroseconds()`;
- millisecond waits where low power is acceptable: `stopMilliseconds()`;
- no fixed-point compensation delay helpers.

### Compensation Data

Initialization should read the BMM350 factory compensation data from the device
and store it in the RAM buffer named by the descriptor. This avoids storing the
factory trim data in tag flash while still making compensation explicit.

The low-level read helper can exist, but normal callers should not need to call
it separately:

```c
msg_t bmm350ReadCompensationData(const TagBmm350Device *dev);
```

`bmm350InitContinuous()` should call this helper after identity/reset and before
entering normal mode. Calibration-state initialization should use the same
driver path with a descriptor whose `compensation` pointer names the calibration
RAM buffer.

The sample conversion path should require valid compensation data:

- `bmm350ReadMagUT()` uses `dev->compensation`.
- If compensation has not been loaded, the read should fail rather than return
  partially compensated values.
- The compensation structure should carry a small validity marker owned by the
  wrapper, not by the Bosch API, so callers can detect whether a buffer has
  been populated.

Suggested local type:

```c
typedef struct {
  bool valid;
  /* BMM350 trim/compensation fields needed by the floating-point converter. */
} TagBmm350Compensation;
```

The field layout should be chosen for the native converter. It does not need to
match Bosch's public structs exactly unless that makes the copied compensation
math clearer.

### Public API Shape

The exact enum names can be adjusted during implementation, but the public API
should stay small:

```c
void bmm350DeviceBegin(const TagBmm350Device *dev);
void bmm350DeviceEnd(const TagBmm350Device *dev);

bool bmm350CheckWhoami(const TagBmm350Device *dev);
msg_t bmm350Reset(const TagBmm350Device *dev);
msg_t bmm350ReadCompensationData(const TagBmm350Device *dev);

msg_t bmm350InitContinuous(const TagBmm350Device *dev,
                           bmm350_data_rate_t rate,
                           bmm350_performance_t performance);
msg_t bmm350InitPowerDown(const TagBmm350Device *dev);

bool bmm350DataReady(const TagBmm350Device *dev);
msg_t bmm350ReadMagUT(const TagBmm350Device *dev,
                      float *mx, float *my, float *mz);
```

The driver should not expose:

- fixed-point conversion functions;
- generic Bosch examples or COINES helpers;
- raw Bosch callback configuration to family code;
- I3C-specific behavior unless a future board needs it.

### Magnetic Reset Policy

Do not run an explicit magnetic reset as part of normal continuous-mode
collection. The BMM350 datasheet describes magnetic reset as occurring on every
ODR tick in normal and forced modes. That makes a normal collection-start reset
unnecessary when the device is configured into continuous normal mode.

An explicit magnetic reset helper may still be useful as a diagnostic or
recovery function, but it should be outside the primary sampling API:

```c
msg_t bmm350RunMagneticResetForRecovery(const TagBmm350Device *dev);
```

Use cases for this optional helper:

- after a long suspend interval when strong-field exposure is suspected;
- in an explicit self-test or diagnostic command;
- as a recovery action after impossible/saturated magnetic readings.

It should not be called every sample, every block, or every normal transition
into continuous collection.

### Data-Ready Policy

Normal sampling should use the wired BMM350 INT/DRDY pin. The family sampling
code should check the GPIO line before reading a sample and should not perform
an I2C status query as part of the regular readiness path.

Driver initialization should configure the BMM350 interrupt output for the
board's chosen active level and drive mode. The descriptor's `drdy` line is
therefore a required part of the normal BMM350 device binding, not an optional
optimization.

Register-level interrupt/status reads may still be useful in self tests or
diagnostic code, but they are not part of the primary sample loop.

### Sample Units

The sample API should return compensated magnetic field values in microtesla as
`float`. Any conversion to a log-storage integer unit should happen above this
driver.

Sample readout should use one burst read for the magnetic data, temperature,
and any sensortime bytes the driver chooses to consume. The datasheet strongly
discourages separate single-register reads in normal mode because data registers
can update between reads and produce an incoherent sample.

### Self Test

Add a BMM350 test hook that performs:

1. power/session begin;
2. chip identity check;
3. init path reads compensation data into the descriptor's RAM buffer;
4. continuous or forced-mode configuration;
5. INT/DRDY line check followed by one sample read attempt;
6. power-down;
7. session end.

The test should return a boolean/status suitable for the existing tag self-test
table.

### Open BMM350 Questions

- What conditions, if any, should trigger the optional magnetic-reset recovery
  helper after the tag has spent time in suspend mode?
- Should normal collection use continuous mode or forced mode paced by the
  existing sensor scheduler?
- What active level and drive mode should the BMM350 INT/DRDY output use on the
  prototype board?

## GD5F1GQ5RE SPI NAND

### Goal

Provide a NAND storage driver with logical page access and bad-block handling.
Callers should not need to know which physical blocks are bad.

The driver should:

- use the existing `TagStorageDevice`/SPI bus lifecycle where possible;
- expose full-page read/write operations;
- expose logical block erase operations;
- build or load a bad-block table;
- map sequential logical pages to physical pages by skipping known-bad blocks;
- never erase or program known-bad physical blocks;
- make erase requests to bad physical blocks no-ops at the raw safety layer.

### Datasheet Basis

The first implementation targets the `GD5F1GQ5RExxG` SPI-NAND family and uses
the following confirmed working geometry and command assumptions:

```text
density:            1 Gbit
page data size:     2048 bytes
spare/OOB size:     64 bytes with internal ECC enabled
pages per block:    64
erase block size:   128 KiB
physical blocks:    1024
minimum good blocks: 1004
read ID:            C8h 41h for GD5F1GQ5RExxG
interface:          SPI x1 initially
```

The driver exposes 1004 logical erase blocks, matching the datasheet's minimum
valid-block guarantee. The BBT is not stored in NAND, so no physical NAND block
needs to be reserved for driver metadata.

### Proposed Files

```text
embedded/tags/common/storage/inc/storage_gd5f1gq5re.h
embedded/tags/common/storage/src/gd5f1gq5re.c
embedded/tags/common/modules/flash_gd5f1gq5re.mk
```

The first pass keeps raw chip commands and logical bad-block translation in one
compact chip driver, behind the existing `TagStorageOps` table. If the NAND
support grows to multiple chips, split the raw command layer and generic BBT
logic into shared files at that point.

### Raw NAND Helpers

The driver keeps physical helpers private:

```c
static bool gd5f1gq5reReadPhysicalPage(...);
static bool gd5f1gq5reProgramPhysicalPage(...);
static bool gd5f1gq5reErasePhysicalBlock(...);
```

Those helpers own:

- command framing;
- write-enable;
- status polling;
- timeout handling;
- cache read/program sequences;
- physical block/page address calculation;
- raw bad-block marker reads;
- compact bad-block-list handling and logical-to-physical calculation.

### Bad-Block Table

The BBT should be kept in CPU flash, not in the SPI-NAND. Exactly when and how
we create the flash-backed table is TBD. The important linker constraint is not
the byte size of the record, because the compact list is small; it is the
STM32L432 internal flash erase-page size. The new `IMUTag` target should reserve
one whole 2 KiB MCU flash erase page for the table and the normal tag data erase
path should avoid that page.

This tag is intended as a one-time-use datalogger, so the firmware does not
update the BBT at runtime. Even during test reuse, we keep the same initial
map. Program operations write only the normal 2048-byte page payload and do not
touch the spare/OOB bytes, preserving factory bad-block markers.

The first implementation uses:

- an optional CPU-flash-resident `TagGd5f1gq5reBadBlockTable` when a later
  target/provisioning step provides the weak `gd5f1gq5reBadBlockTable` symbol;
- a RAM bad-block list populated from that CPU table;
- a factory-marker scan fallback for bring-up if the CPU table is absent or
  invalid.

Boot behavior:

1. Reset and read ID.
2. Unlock array writes by clearing the block-lock feature register.
3. Try to load and validate the CPU-flash BBT.
4. If no valid CPU BBT exists, scan factory bad-block markers into RAM before
   any erase.
5. Map each sequential logical block by counting physical good blocks and
   skipping the sorted bad-block list.

BBT record fields:

```text
magic: 0x54424247
version
physical block count
logical block count
bad block count
FNV-1a checksum over little-endian bad block count plus bad-block list
uint16 little-endian sorted physical bad-block list, up to 32 entries
```

Bad-block rules:

- Factory-marked blocks are always bad.
- Known-bad blocks are never erased or programmed.
- A logical erase request beyond the exposed logical capacity returns success
  without issuing an erase command.
- Program and erase failures return failure. The firmware does not retire newly
  failed blocks or persist a new BBT generation.
- Read failure policy should initially return failure; later work can decide
  whether to retire blocks on repeated read/ECC errors.

### Storage Abstraction

The existing NOR `TagStorageOps` is byte-addressed, so the first NAND driver
uses the same table while enforcing NAND-safe writes:

- `sector_size` is the logical NAND erase-block size.
- `sector_count` is `GD5F1GQ5RE_LOGICAL_BLOCK_COUNT`.
- `read` supports arbitrary byte offsets by reading the needed cache page(s).
- `write` only accepts full-page-aligned, whole-page writes.
- partial page writes fail rather than silently programming invalid NAND state.

### Self Test

Add a NAND test hook that performs:

1. read ID and verify expected bytes;
2. load or build BBT;
3. report logical capacity;
4. erase one test logical block;
5. write one full logical page;
6. read it back and compare;
7. erase the test logical block again.

The test must choose a logical block reserved for testing or run only when the
normal log area may be destroyed.

### Open NAND Questions

- Confirm the exact `GD5F1GQ5REYFGR` datasheet and final geometry.
- Decide the final CPU-flash section name and provisioning workflow for
  `gd5f1gq5reBadBlockTable`.
- Reserve/protect one whole STM32L432 internal flash erase page, 2 KiB, for the
  BBT even though the compact record itself is much smaller.
- Datalog page integrity relies on NAND ECC. Logical-page tracking and recovery
  are handled by periodic MCU-flash headers, for example one header every N
  logical NAND pages.
- Decide whether quad-SPI read/program support is worth adding after x1 SPI
  bring-up.
