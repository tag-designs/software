# Sensor Drivers

`sensors` contains reusable sensor drivers and sensor-side bus adapters. A
driver belongs here only when it is genuinely reusable across tags. Drivers
that encode one tag family's wakeup policy, mounting quirks, or transaction
sequence should live in that tag family instead.

## Layout

- `inc/sensor_io.h`, `src/sensor_io.c`: common register-bus adapters. These
  wrap low-level bus helpers from `core` into sensor-oriented register reads
  and writes.
- `accel/`: accelerometer drivers that are reusable enough to be common.
- `pressure/`: LPS/BMP pressure drivers plus the `lps.c` compatibility shim.
- `mag/`: AK09940A descriptor driver, default shim, and legacy MMC5633 code.
- `light/`: light sensor drivers.

## Descriptor Pattern

Newer reusable drivers take a device descriptor rather than directly naming
board lines. The descriptor normally contains:

- a `TagRegisterBus` for register reads/writes;
- bus-session callbacks such as `bus_begin` and `bus_end`;
- device power callbacks only when the sensor driver truly controls power
  lifetime;
- optional sensor-specific callbacks, such as trigger or data-ready lines.

The split matters. Bus begin/end is not the same as device power on/off, and
chip select framing is part of the device protocol. Keep command bytes and
payloads under the same chip-select assertion when the datasheet expects one
transaction.

## Shims

Some drivers still expose older names such as `lpsGetPressureTemp()` or
`magSample()`. Those names should live in small shim files that bind the
compiled tag's default descriptor and call the descriptor-driven implementation.
This keeps legacy call sites working while allowing new tag/family code to use
explicit descriptors directly.

## Family-Specific Drivers

The CompassTag LIS2DU12 code intentionally lives in
`families/CompassTag`, not here. Its wakeup setup and synchronous-USART
transaction framing are specialized to that family.
