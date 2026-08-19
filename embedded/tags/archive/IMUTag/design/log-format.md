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
| 100 Hz | 10 Hz | 12.5 Hz | 25 Hz | Safe margin (+2.5 Hz overhead) |
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
- If a fresh pressure or magnetometer sample is not available for an auxiliary
  slot, firmware writes the explicit IEEE-754 quiet-NaN bit pattern
  `0x7fc00000` for the missing float value.
- NAND internal ECC is responsible for page-level data integrity, so the 2048
  byte payload does not need its own checksum.
- Firmware should not batch the full 2048-byte page write at high IMU rates.
  The wake timestamp that made the previous superframe available is retained as
  the start timestamp for the next superframe. When that next superframe starts
  a page, the retained timestamp becomes the page header. Firmware then reads
  and fills the 136-byte superframe before doing any flash writes for that wake,
  writes the page header together with that first superframe, then writes later
  superframes incrementally. The processor/internal header is written as soon
  as the first retained superframe has been written. For NAND this separates
  incremental cache writes from the eventual physical page program operation.
- NAND cache writes must use the multi-step page-program sequence directly:
  use Program Load (`02h`) for the initial page header plus first superframe.
  This sets the starting column and initializes all unwritten bytes in the
  volatile page buffer to `0xff`. Use Program Load Random Data (`84h`) for
  each remaining superframe; this changes the column pointer and overwrites
  only the streamed byte range, preserving previously loaded cache data. After
  all 15 superframes have been loaded, issue Program Execute (`10h`) for the
  target page address. This final command performs the single physical write
  cycle to the NAND array.
- Sparse checkpoints in MCU flash track seconds, logical page number, and flags
  for restart/erase recovery. They do not need `millis`, because each NAND page
  carries its own seconds plus 1/1024 s subsecond timestamp.
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
  Slow_Sample_t slow_data; /* 1 * 8 bytes    = 8 bytes */
  SuperFrame_t frames[15]; /* 15 * 136 bytes = 2040 bytes */
} Nand_Page_t;
```

## Time Synchronization

All timing in the tag is derived from the RTC and its 1024 Hz signal. This
signal drives the IMU in ODR-triggered mode. The IMU then derives its internal
sampling rate from that reference and aligns data generation in frequency and
phase to the reference edges. Given a page timestamp in seconds plus 1/1024 s
subsecond ticks, the host can reconstruct accurate sample timestamps.

Every page starts with a slow/recovery sample containing an RTC timestamp. Call
that timestamp `t0`. It is the boundary immediately before the first IMU sample
stored in the page, not the timestamp of the flash write itself.

There are 150 IMU samples in a page, indexed `i = 0..149`. For a configured IMU
sample rate `sample_rate`:

```text
delta = 1 / sample_rate
time(imu[i]) = t0 + (i + 1) * delta
```

The page contains 15 superframes. Each superframe contains 10 IMU samples and
one auxiliary pressure/magnetometer sample, so auxiliary data is logged at 1/10
the IMU rate. For auxiliary sample `j = 0..14`:

```text
time(mag[j]) = t0 + (j + 1) * 10 * delta
time(pressure[j]) = t0 + (j + 1) * 10 * delta
```

The pressure and magnetometer parts are free-running and are polled when each
superframe is assembled. The timestamp above is therefore the log-time assigned
to the auxiliary slot: the end boundary of the corresponding 10-sample IMU
superframe. If a fresh auxiliary conversion is not available for that slot,
firmware writes the explicit NaN value described above.

Firmware captures `t0` by retaining the timestamp from the wake that made the
previous superframe available. That wake marks the beginning of the next
superframe. When the next superframe becomes the first superframe of a new NAND
page, the retained timestamp becomes the page timestamp:

```text
wake N
timestamp_N = get_timestamp()
read/process the superframe that just became available
retain timestamp_N as the start boundary for the next superframe

wake N+1
read/process the next superframe
if this superframe starts a new page:
    page.t0 = retained timestamp_N
```

The pressure-sensor temperature stored in the page header should have the same
logical time as `t0`. If firmware samples pressure and temperature while
assembling the first superframe, it should copy that freshly read temperature
into the page header before the first page-program load. Otherwise the header
temperature is only a recent slow value, not an exact `t0` sample.
