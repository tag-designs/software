# IMUTag NAND Log Format Design

This is a design idea for the IMUTag logging format.

## Assumptions

- A 2048-byte NAND page is the unit of logging.
- Every page includes RTC seconds/milliseconds for recovery and resynchronization.
- Magnetometer and pressure are logged at 1/N the IMU rate.
- Pressure sensor temperature is logged once per page.
- The 50 Hz IMU sample rate is intentionally omitted. There is little energy
  advantage over 100 Hz, and the large NAND removes the old storage-pressure
  reason to keep the slower mode.

## Unified Frame Structure

If the auxiliary sensors, magnetometer and pressure, are read at 1/10 the IMU
rate, their configured ODRs should remain above that polling frequency.

| Selected LSM6 Rate (RTC Sync) | Resulting Polling Freq. (1:10) | Safe BMM350 ODR | Safe LPS22HH ODR | Hardware Safety Margin |
|---|---:|---|---|---|
| 100 Hz | 10 Hz | 12.5 Hz | 12.5 Hz | Safe margin (+2.5 Hz overhead) |
| 200 Hz | 20 Hz | 25 Hz | 25 Hz | Safe margin (+5 Hz overhead) |
| 400 Hz | 40 Hz | 50 Hz | 50 Hz | Safe margin (+10 Hz overhead) |
| 800 Hz | 80 Hz | 100 Hz | 100 Hz | +25% safety margin (100 Hz vs 80 Hz) |
| 1600 Hz | 160 Hz | 200 Hz | 200 Hz | +25% safety margin (200 Hz vs 160 Hz) |

## Benefits

- Fixed page math: every NAND page contains exactly 15 super-frames at 136 bytes
  each, filling 2040 bytes and leaving 8 bytes for slow/recovery data.
- One unified page layout works across all supported IMU rates.
- Auxiliary data is stored as 32-bit floats. The BMM350 compensation path is
  naturally floating point, and LPS22HH pressure does not lose meaningful
  precision because IEEE-754 single precision carries effectively 24 bits of
  significand precision.
- Pressure temperature is assumed to move slowly relative to pressure, so one
  LPS22HH temperature sample per NAND page is sufficient even though page
  duration varies with IMU rate.
- Auxiliary samples are over-provisioned relative to the 1:10 polling cadence,
  so a fresh pressure and magnetic sample should be available when the log
  loop reaches the auxiliary slot.
- NAND internal ECC is responsible for page-level data integrity, so the 2048
  byte payload does not need its own checksum.
- Periodic headers in MCU flash, for example one header every N logical NAND
  pages, track logical page position and provide coarse recovery anchors.
- NAND capacity is large enough that the log format can prioritize simple
  parsing and robust recovery over byte-level compression.

## Timing Notes

Each page contains 150 IMU samples. Page duration therefore depends on the
selected IMU rate:

| LSM6 Rate | IMU Samples/Page | Page Duration | RTC Ticks at 1024 Hz |
|---:|---:|---:|---:|
| 100 Hz | 150 | 1.500 s | 1536 |
| 200 Hz | 150 | 0.750 s | 768 |
| 400 Hz | 150 | 0.375 s | 384 |
| 800 Hz | 150 | 0.1875 s | 192 |
| 1600 Hz | 150 | 0.09375 s | 96 |

The underlying real-time clock runs at 1024 Hz. For every supported IMU sample
rate, one 2048-byte NAND page spans an integer number of RTC ticks. That makes
page-level timing recovery self-contained: a decoder can reconstruct page
start/end timing from the page's RTC timestamp, the configured sample rate, and
the fixed 150-sample page structure without accumulating fractional-tick error.

Because page duration is rate-dependent, the 8-byte slow-data slot is best
treated as page recovery metadata plus optional slow data, not as a guaranteed
exact 1 Hz record by itself.

## Proposed C Layout

```c
#include <stdint.h>

/* 12 bytes: sampled at full IMU rate, 100 Hz to 1600 Hz. */
typedef struct __attribute__((packed)) {
  int16_t accel_x;
  int16_t accel_y;
  int16_t accel_z;
  int16_t gyro_x;
  int16_t gyro_y;
  int16_t gyro_z;
} IMU_Sample_t;

/* 16 bytes: auxiliary data, sampled at 1/10 IMU rate. */
typedef struct __attribute__((packed)) {
  float pressure; /* hPa */
  float mag_x;    /* compensated BMM350 uT */
  float mag_y;
  float mag_z;
} Aux_Sample_t;

/* 136 bytes: 10 IMU updates plus one auxiliary update. */
typedef struct __attribute__((packed)) {
  IMU_Sample_t imu[10]; /* 10 * 12 bytes = 120 bytes */
  Aux_Sample_t aux;     /* 1 * 16 bytes  = 16 bytes */
} SuperFrame_t;

/* 8 bytes: page recovery metadata and low-rate data. */
typedef struct __attribute__((packed)) {
  int32_t rtc_seconds;
  uint16_t rtc_millis;
  int16_t baro_temp; /* LPS22HH internal temperature register */
} Slow_Sample_t;

/* 2048 bytes: one NAND flash page. */
typedef struct __attribute__((packed)) {
  SuperFrame_t frames[15]; /* 15 * 136 bytes = 2040 bytes */
  Slow_Sample_t slow_data; /* 1 * 8 bytes    = 8 bytes */
} Nand_Page_t;
```
