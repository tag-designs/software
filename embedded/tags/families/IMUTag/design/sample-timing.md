---
type: proposal
status: proposed
summary: Staged plan for jitter-free IMU sampling from raw RV-3028 clock with RTC smooth correction and timing metadata; reconstruction is partly built.
---

# Strategy for Jitter-Free Sampling with Smooth Real-Time Correction

## Purpose

IMUTag variants currently use the RV-3028 compensated CLKOUT as the low-speed
reference for the STM32 RTC and for the LPTIM-derived LSM6DSV16X external ODR
trigger. The current compensated divided CLKOUT preserves long-term RTC
accuracy, but the RV-3028 correction pulses add significant jitter to the
sampling path.

This note describes a staged strategy to keep sampling jitter-free while
preserving accurate, reconstructable wall-clock time:

1. Use the raw, uncorrected 32.768 kHz RV-3028 signal as the STM32 LSE input.
2. Use this raw LSE-derived signal to drive the LPTIM/LSM6DSV16X sampling chain.
3. Use the STM32U375 RTC smooth-correction hardware to transfer real-time clock
   correction from the RV-3028 divided output into the STM32 RTC calendar path.
   The required correction data is compatible with the factory RV-3028
   correction data stored in EEPROM.
4. Include the RV-3028 correction data in log/download metadata so host software
   can reconstruct smooth corrected wall-clock time for sampled data.

IMUTag data are stored in pages, and each page carries a compact RTC timestamp
in its header. In continuous recording, those per-page timestamps should be
treated as checkpoints rather than as the sample clock: sample timing is better
reconstructed from the configured ODR and the number of samples since a known
start point. The page timestamps and flags are still important because they let
the downloader detect boundaries where continuity may have been broken, such as
collection start, restart recovery, storage skips, or explicit resync points. A
`segment` is one continuous run of samples whose timestamps can be computed from
one start point plus sample count. A `segment anchor` is the page header or
event record that establishes that segment's wall-clock placement.

The following are the key development guidelines:

- First development step: change only the firmware RTC correction path and
  LPTIM prescaler so power impact can be measured before metadata, downloader,
  or SQLite reconstruction work begins.
- The active log contract keeps IMUTag logged page timestamps in the existing
  1024 Hz subsecond domain.
- Host software reconstructs sample timing from segment anchors, sample counts,
  configured ODR, and explicit resync/restart boundaries.
- Download/import stores the major raw headers and segment anchors needed to
  audit or reconstruct the timing model from the SQLite file later.
- Non-log public timestamps remain in the existing 1024 Hz-compatible domain.
- LSM6DSV16X trigger output frequencies remain unchanged.

The goal is first to make data timing independent of ordinary per-page timestamp
jitter. After reconstruction, per-page timestamps are anchors and diagnostics,
not the sample clock.

## Target Contract

The active plan keeps the current packed page timestamp domain:

```text
IMUTag log page subsecond ticks: 1024 Hz
```

Host software reconstructs high-rate sample time as:

```text
segment_start_time + sample_index / configured_odr
```

Within a continuous segment, per-page timestamps are diagnostic checkpoints, not
the primary sample clock. Missed pages can be represented as missing sample
ranges whose times are still computed from the same sample index progression.

Per-page timing data is used as an anchor only when continuity is broken or
uncertain:

```text
collection start
restart/recovery resync
explicit storage discontinuity
diagnostic comparison against reconstructed page time
```

Phase 1 switches IMUTag firmware to the direct 32.768 kHz RV-3028 clock path.
Public timestamp surfaces outside the IMUTag raw log still remain in the
1024 Hz convention.

The LSM6DSV16X trigger driver continues to request logical divisors in the
1024 Hz domain. With the physical LPTIM source at 32.768 kHz, the IMUTag board
timer layer uses the LPTIM prescaler to present the same 1024 Hz timer counting
domain to the ARR/CMP divider code, so the physical trigger signal delivered to
the sensor does not change.

## Accuracy Impact

The initial reconstruction step keeps the current page-anchor quantization:

| Timebase | Tick period | Half-tick quantization |
| --- | ---: | ---: |
| 1024 Hz | 976.5625 us | about 488 us |

For continuous data, timing accuracy comes from the reconstructed sample grid,
not from every page header. Page-anchor quantization matters mainly when
starting a new segment after collection start, restart recovery, or an explicit
resync/discontinuity.

Reconstruction does not reduce RTC set-time uncertainty, MCU wake latency, FIFO
watermark latency, or any sample-to-sample jitter introduced by the sensor. The
smooth RV-3028/STM32 RTC plan below specifically targets compensation-pulse
jitter in the shared clock source.

## Smooth RV-3028 Compensation Plan

Decision and rationale: see [0002-imutag-rtc-smooth-calibration-over-compensated-clkout.md](../../../../../docs/decisions/0002-imutag-rtc-smooth-calibration-over-compensated-clkout.md).

### Factory Offset Metadata

The RV-3028 factory offset is the 9-bit `EEOffset` value:

- upper 8 bits: EEPROM Offset register `0x36`, bits `EEOffset[8:1]`;
- low bit: EEPROM Backup register `0x37`, bit `EEOffset[0]`.

Decode as signed 9-bit two's-complement steps:

```c
uint16_t raw9 = ((uint16_t)offset_reg << 1) |
                ((backup_reg >> 7) & 0x01u);
int16_t steps = (raw9 & 0x100u) ? (int16_t)(raw9 - 512) : (int16_t)raw9;
```

Each step is approximately `0.9537 ppm`, and the maximum representable
correction is about `+243.187 ppm` / `-244.141 ppm`. That maximum correction is
about `21.1 s` over 24 hours:

```text
86400 s * 244 ppm / 1,000,000 = 21.0816 s
```

This correction range is large enough for the raw crystal and temperature curve
we expect to encounter. The host should store both the raw step count and the
derived ppm value so the calculation is auditable.

### STM32 RTC Smooth Calibration

STM32U375 RTC smooth calibration can apply a correction with the same nominal
step size as the RV-3028 factory offset. This gives the tag an accurate
operator-facing real-time clock while preserving a smooth raw LSE-derived clock
for the LSM6DSV16X trigger.

Firmware should translate the RV-3028 `EEOffset` into STM32 RTC calibration
register fields and store both the source value and applied STM32 values:

```text
rv3028_eeoffset_steps
rv3028_correction_ppm
stm32_rtc_calp
stm32_rtc_calm
stm32_rtc_calibration_window_seconds
```

The exact sign mapping must be verified during implementation against the
RV-3028 offset convention and STM32 `RTC_CALR` behavior. The expected shape is:

```c
if (steps == 0) {
    calp = false;
    calm = 0;
} else if (steps < 0) {
    calp = false;
    calm = (uint16_t)(-steps);
} else {
    calp = true;
    calm = (uint16_t)(512 - steps);
}
```

When STM32 RTC calibration is active:

- RTC-derived event timestamps are already corrected wall-clock estimates.
- LPTIM/LSM sample timing is still raw-clock timing and needs the stored ppm
  correction when exporting reconstructed wall-clock sample timestamps.
- Host software must not apply the raw-clock correction a second time to event
  timestamps that came from the calibrated RTC calendar.

### Operator-Facing Explanation

Operators should see this as a data-quality improvement, not as an uncalibrated
clock:

```text
The tag samples from a smooth uncompensated clock to avoid timing jitter.
The tag's RTC applies the factory calibration for real-time event timestamps.
Download software preserves the raw headers and applies the calibration to the
sample timeline when exporting wall-clock sample times.
```

For scheduled operation, the tag can use the STM32 RTC calibrated wall time.
The important distinction is:

- firmware records major events from the corrected RTC calendar;
- high-rate logged sample timing remains on the raw smooth LSE/LPTIM grid;
- host software applies the stored ppm correction to sample elapsed time when
  presenting/exporting wall-clock sample timestamps.

### Major Headers and Download Reconstruction

The primary download/import plan is to preserve the raw segment structure, not
just final computed sample timestamps. Because STM32 RTC smooth calibration
moves the real-time correction into the RTC calendar path, the log does not need
a separate "time was set at" correction anchor. Segment anchor times come from
the corrected RTC page headers and event records already stored in the log. The
additional metadata needed for sample-time reconstruction is the correction
factor that maps raw LSE/LPTIM sample elapsed time onto corrected wall-clock
elapsed time:

```c
typedef struct {
    int16_t rv3028_eeoffset_steps;      /* Signed factory offset steps. */
    int32_t correction_ppb;             /* Derived correction in parts/billion. */
    uint8_t rv3028_clock_mode;          /* Compensated divided or direct 32.768 kHz. */
    uint8_t stm32_rtc_smooth_enabled;   /* RTC calendar already corrected. */
    bool    stm32_rtc_calp;             /* Applied STM32 positive calibration bit. */
    uint16_t stm32_rtc_calm;            /* Applied STM32 minus calibration field. */
} t_RtcCorrectionMetadata;
```

Firmware should persist this correction metadata and include it with the
downloaded log metadata. It should record whether the RV-3028 output used during
logging was the compensated divided CLKOUT or the direct 32.768 kHz CLKOUT, and
whether STM32 RTC smooth calibration was active. Firmware does not need to apply
any sample-grid correction while running.

When STM32 RTC smooth calibration is active, the host reconstructs sample wall
time from the segment anchor and corrected raw sample elapsed time:

```text
raw_sample_elapsed_seconds = samples_since_segment_anchor / configured_odr
correction_ppm = rv3028_eeoffset_steps * 0.9537
corrected_sample_elapsed_seconds =
    raw_sample_elapsed_seconds * (1 + correction_ppm / 1e6)
sample_wall_time =
    corrected_segment_anchor_time + corrected_sample_elapsed_seconds
```

RTC-derived low-rate events are already in the corrected calendar domain. Host
software should preserve their raw header values and calibration metadata, but
should not apply the raw sample-clock correction to those events again.

The IMUTag raw data log should keep raw page-anchor ticks in the selected log
subsecond domain and store the correction metadata once for host
post-processing.

If the tag is still using the RV-3028 compensated divided CLKOUT, host software
should store the `EEOffset` for audit but not apply the same correction again.

If runtime-corrected scheduling later becomes necessary beyond the STM32 RTC
calendar correction, it can be added as a separate policy layer using the same
correction factor. That path should use fixed-point integer math, not floating
point, in firmware, and should stay out of the hot sample/log loop.

### Additional Firmware Changes

Files:

- `embedded/tags/common/rtc/inc/rv3028.h`
- `embedded/tags/common/rtc/src/rtc_rv3028.c`
- `embedded/tags/common/core/inc/timekeeping.h`
- `embedded/tags/common/core/src/time.c`
- `embedded/tags/families/IMUTag/inc/persistent.h`
- `embedded/tags/families/IMUTag/src/config.c`
- `embedded/tags/families/IMUTag/src/state_run.c`
- `embedded/tags/families/IMUTag/src/datalog.c`
- `host/libraries/tagcore/sqlitelog/imutag.cc`
- `host/libraries/tagcore/sqlitelog/schema.cc`

Add RV-3028 read support:

```c
bool rv3028ReadEEOffset(const TagRtcDevice *device, int16_t *steps);
```

Add correction metadata helpers:

```c
bool rtcCorrectionMetadataCapture(t_RtcCorrectionMetadata *metadata);
```

Persist the correction metadata after RTC initialization/configuration. Store
the `EEOffset` steps, derived correction value, RTC clock mode, and applied
STM32 `CALP/CALM` values in persistent state and include them in downloaded log
metadata or SQLite output.

Host decode should expose both raw and corrected timing:

- raw elapsed time remains the primary storage domain;
- corrected elapsed/wall time can be used for export and user display;
- metadata should record `rv3028_eeoffset_steps`, correction ppm, and the
  correction mode (`host_linear` or `stm32_rtc_smooth`).

## Cross-Tag Isolation Macros

Add these macros so the IMUTag change is explicit and non-IMUTag tags keep
their current build-time assumptions.

### RTC Reference Macros

Define the raw RTC/reference frequency from the target RTC configuration. With
the smooth compensation plan, IMUTag uses a 32.768 kHz RTC reference. The first
smooth-clock implementation should preserve the 1024 Hz RTC subsecond counter:

```c
#define TAG_RTC_REFERENCE_HZ \
  (STM32_RTC_PRESA_VALUE * STM32_RTC_PRESS_VALUE)

#define TAG_RTC_SUBSECOND_HZ STM32_RTC_PRESS_VALUE
```

`TAG_RTC_REFERENCE_HZ` is used for RV-3028 CLKOUT selection and LPTIM trigger
prescaler selection. `TAG_RTC_SUBSECOND_HZ` names the RTC SSR tick domain used
when reading raw subsecond ticks. In the first smooth-clock phase these differ:

```text
TAG_RTC_REFERENCE_HZ = 32768
TAG_RTC_SUBSECOND_HZ = 1024
```

### STM32 RTC Calibration Macros

Make the real-time correction mode explicit in IMUTag builds:

```c
#ifndef IMUTAG_USE_STM32_RTC_SMOOTH_CALIBRATION
#define IMUTAG_USE_STM32_RTC_SMOOTH_CALIBRATION 0
#endif

#define IMUTAG_SAMPLE_CLOCK_CORRECTION_SOURCE_RV3028_EEOFFSET 1
```

When `IMUTAG_USE_STM32_RTC_SMOOTH_CALIBRATION` is enabled, firmware programs
STM32 `RTC_CALR` from the RV-3028 factory offset and records the applied
`CALP/CALM` values in log metadata. The LSM trigger remains downstream of the
raw LSE-derived LPTIM path, so this macro must not change LSM trigger divisors.

### Public Timestamp Macros

Keep public timestamps in the legacy-compatible domain:

```c
#define TAG_PUBLIC_SUBSECOND_HZ 1024u
```

When `TAG_RTC_SUBSECOND_HZ != TAG_PUBLIC_SUBSECOND_HZ`, firmware scales raw
subseconds before filling monitor/status or other non-log protobuf time fields.

### IMUTag Log Timestamp Macros

Define the IMUTag raw-log timebase explicitly. For this strategy the active
log format remains 1024 Hz:

```c
#define IMUTAG_LOG_SUBSECOND_HZ 1024u
#define IMUTAG_LOG_SUBSECOND_BITS 10u
#define IMUTAG_LOG_SUBSECOND_MASK 0x03ffu
```

To keep the code cleanup incremental, keep the historical mask name as an alias:

```c
#define IMUTAG_HEADER_MILLIS_MASK IMUTAG_LOG_SUBSECOND_MASK
```

New or touched code should use `IMUTAG_LOG_SUBSECOND_MASK` because the field is
not milliseconds.

Keep header flags in their existing positions:

```c
#define IMUTAG_HEADER_RESYNC 0x0400u
#define IMUTAG_HEADER_RESYNC_STORAGE_SKIP 0x0800u
#define IMUTAG_HEADER_RESTART_RECOVERY 0x1000u
```

The binary layout remains unchanged:

```text
mask = 0x03ff, flags = 0x0400, 0x0800, 0x1000
```

### LSM6 Trigger Prescaler Macros

Keep the LSM6DSV16X common driver in its existing logical 1024 Hz domain:

```c
#define IMUTAG_IMU_TRIGGER_LOGICAL_HZ 1024u
#define IMUTAG_IMU_TRIGGER_TIMER_HZ TAG_RTC_REFERENCE_HZ
#define IMUTAG_IMU_TRIGGER_LPTIM_PRESCALER_DIV \
  (IMUTAG_IMU_TRIGGER_TIMER_HZ / IMUTAG_IMU_TRIGGER_LOGICAL_HZ)
```

Compile-time checks should reject non-integer prescaling:

```c
#if (IMUTAG_IMU_TRIGGER_TIMER_HZ % IMUTAG_IMU_TRIGGER_LOGICAL_HZ) != 0
#error "IMUTag trigger timer frequency must be an integer multiple of 1024 Hz"
#endif
```

For direct 32.768 kHz RTC/reference operation,
`IMUTAG_IMU_TRIGGER_LPTIM_PRESCALER_DIV` is 32. The only LSM trigger setup
change should be the LPTIM prescaler field; ARR/CMP continue to use the same
1024 Hz-domain divider values as before.

## Implementation Plan

History: see [proposals/sample-timing-implementation-plan.md](proposals/sample-timing-implementation-plan.md).

## Open Decisions

- Whether archived L432 IMUTag variants should remain historical only during
  rollout.
- Whether the smooth direct-RV3028 phase should ship with the conservative
  `PRESA/PRESS = 32/1024` setting for all active STM32U3 IMUTag variants.
- Whether to store only major reconstruction headers or every downloaded page
  header in SQLite for diagnostics.
- Whether SQLite should add raw `SubsecondTicks/SubsecondHz` columns immediately
  or only use them internally to compute `Millisecond`.
- Whether the historical `millis` field names should be renamed in a future
  binary-format revision. This plan keeps names unchanged to avoid changing the
  packed page layout.

## Future Consideration: 8192 Hz Restart Anchors

If reconstruction and smooth-clock testing show that 1024 Hz restart-anchor
quantization is a real limitation, IMUTag could later move logged page
subseconds to 8192 Hz. That would reduce half-tick restart-anchor quantization
from about `488 us` to about `61 us`, but it would not improve continuous
sample timing because continuous timing already comes from the reconstructed
sample grid.

That future change would require:

- changing active IMUTag `mcuconf.h` RTC values to `PRESA/PRESS = 4/8192`;
- setting `IMUTAG_LOG_SUBSECOND_HZ` to `8192u` for IMUTag builds;
- moving packed-header flags from `0x0400/0x0800/0x1000` to
  `0x2000/0x4000/0x8000`;
- updating protobuf comments and host decoding constants to the 8192 Hz
  contract;
- preserving public/status timestamps by scaling raw 8192 Hz subseconds down to
  the 1024 Hz public domain.
