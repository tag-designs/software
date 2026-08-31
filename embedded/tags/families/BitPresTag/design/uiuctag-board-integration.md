# UIUCTag Board Integration Plan

## Scope

This note tracks the firmware work needed to integrate the **UIUCTag** board
into the BitPresTag family. UIUCTag differs from the baseline BitPresTag in two
key ways:

1. **Accelerometer**: Uses **ADXL367** instead of ADXL362, connected through
   **USART2 in synchronous 4-wire SPI mode**.
2. **Pressure sensor**: Uses **BMP585** (software-compatible with BMP581)
   instead of LPS27, connected through **SPI1**, with an **LPS_RDY** interrupt
   line for data-ready and power-on-reset detection.

The board is a BitPresTag-family variant: it reuses the family state machine,
test framework, storage modules, and ChibiOS configuration pattern while
overriding board device descriptors and the eventual data-log layout.

## Existing BitPresTag Model

The current BitPresTag family uses:

- **ADXL362** on SPI2, configured in wake-mode for activity detection.
- **LPS27** pressure sensor on USART1 in SPI mode, polled during RTC wake
  intervals.
- Single-wire wake sources: ADXL362 for activity, with no pressure data-ready
  interrupt.

The LPS27 path powers the sensor, starts a one-shot conversion, polls status,
reads the sample, and powers down before returning.

## UIUCTag Hardware Differences

| Component | BitPresTag baseline | UIUCTag variant |
|-----------|---------------------|-----------------|
| Accelerometer | ADXL362 on SPI2 | ADXL367 on USART2 synchronous 4-wire SPI |
| Pressure sensor | LPS27 on USART1 synchronous SPI | BMP585 on SPI1 |
| Pressure interrupt | None | LPS_RDY EXTI line |

### ADXL367 on USART2

The ADXL367 driver uses the shared `TagBusDevice` transport abstraction, so the
board descriptor may bind the accelerometer to either a normal SPI device or a
USART synchronous bus. UIUCTag binds it with `TAG_BUS_USART_INIT()` using the
generated `LINE_ACCEL_nCS`, `LINE_ACCEL_SCK`, `LINE_ACCEL_MOSI`, and
`LINE_ACCEL_MISO` board lines.

USART2 must be configured for synchronous 4-wire operation compatible with the
ADXL367 SPI mode:

- CPOL = 0, CPHA = 0.
- TX/RX enabled.
- Chip select controlled by the board descriptor GPIO.
- Sleep policy leaves the bus pins in the safe idle state before standby.

### BMP585 on SPI1 with LPS_RDY

The BMP585 is software-compatible with BMP581. The shared
`embedded/tags/common/sensors/pressure/src/bmp581.c` driver supports both chip
IDs (`0x50` and `0x51`). UIUCTag binds this sensor through a `TagRegisterDevice`
using `TAG_REGISTER_ST` register semantics on SPI1.

The LPS_RDY line must be wired to an EXTI channel and configured as:

- Active-low falling edge for DRDY/POR.
- Latched interrupt mode on the BMP585.
- External pull-up, or an internal pull-up if the board does not provide one.

## Firmware Integration State

Implemented skeleton:

- `embedded/tags/UIUCTag/`: firmware target wrapper, local `custom.h`, and
  board-specific device descriptors.
- `embedded/boards/UIUCTag/`: generated board configuration source.
- `include/uiuctag_log_format.h`: shared packed C structs for the UIUCTag log
  and download format.
- `proto/tagdata.proto`: `UIUCTAG` tag type and `UIUCTagLog` protobuf messages.
- `proto/tag.proto`: `Ack.uiuctag_data_log` response payload.
- `embedded/proto-c/uiuctag-proto-c/`: UIUCTag nanopb target and default
  `UIUCTAG` configuration.

Data collection and the download path are implemented; see the
[UIUCTag data collection integration plan](uiuctag-data-collection.md) for the
record format, the write sequencing, the host decoder, and what still needs a
hardware run.

## Log Schema

UIUCTag does not use the existing BitPresTag decoded log layout. It uses compact
fixed-size C records in firmware and matching protobuf messages for host
download.

### Timing

- Activity is accumulated into one-minute buckets.
- Each one-minute bucket stores a six-bit activity value.
- One sample covers five minutes and packs five activity buckets into a
  `uint32_t`.
- One data-log block covers 24 five-minute samples, or two hours, matching
  `UIUCTAG_LOG_SAMPLES` and the `UIUCTagLog.samples` protobuf bound.

### Shared C Format

The canonical binary layout lives in `include/uiuctag_log_format.h`.

```c
typedef struct {
    float pressure;
    float temperature;
    uint32_t packed_activity_data;
} t_UIUCTagSample;

typedef struct {
    int32_t epoch;
    float voltage;
    t_UIUCTagSample samples[24];
} t_UIUCTagDataLog;
```

The external block number is stored in the checkpoint even though it currently
equals the checkpoint index, so the mapping stays explicit rather than implied. The packed
sizes are:

- `t_UIUCTagSample`: 12 bytes.
- `t_UIUCTagDataLog`: 296 bytes.

### Protobuf Download Format

The matching protobuf messages are:

- `UIUCTagLog`

UIUCTagLog should have the form:

```c
message UIUCTagLog{
  int32 epoch = 1;
  float voltage = 2;
  bytes samples = 3; // t_UIUCTagSample[24]. -- max size = 12*24
}
```

The internal log is used to track voltage and external log location and so has the form:

```c
  int32_t epoch;
  uint16_t raw_voltage;
  uint16_t extern_log_block; // 12*24 sized offset
```

In the event that fewer external blocks have been written, the size of the raw samples will be lower to reflect that.  Protobuf naturally returns the number of bytes.

### Staged External Writes

Each five-minute sample is written in two phases:

1. At the five-minute pressure sample, write `pressure`, and BMP585
   `temperature`.
2. Just before the next external block starts, write `packed_activity_data` for
   the preceding five one-minute activity buckets.

External storage writes should be broken into four-byte chunks with a rest
period between chunks so the storage capacitor can recharge.

## Validation Sequence

1. `RUN_ADXL362` exercises the ADXL367 test case for the USART2 accelerometer
   binding.
2. `RUN_LPS` probes the BMP585-compatible chip ID through the BMP581 driver.
3. A logic analyzer confirms ADXL367 synchronous USART transactions.
4. GPIO/EXTI monitoring confirms LPS_RDY wakes on BMP585 DRDY.
5. A full RUNNING capture verifies RTC wake, pressure conversion, activity
   bucketing, staged writes, and download reconstruction.
6. Power analysis compares the staged write/recharge policy against available
   storage capacitance.

## Related Design Notes

- [BMP581/BMP585 forced-mode pressure plan](bmp581-forced-mode.md): Driver and
  BitPresTag-family integration plan for interrupt-driven forced pressure
  sampling.
- [ADXL367 driver](../../../../common/sensors/accel/inc/ADXL367.h):
  Descriptor-backed accelerometer driver using shared sensor bus transports.
- [BMP581 driver](../../../../common/sensors/pressure/inc/bmp581.h):
  Descriptor-backed pressure driver supporting BMP581/BMP585 chip IDs.
