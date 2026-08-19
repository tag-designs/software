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

The RV-3028 can apply its factory frequency correction by inserting or removing
compensation pulses on divided CLKOUT frequencies. That preserves long-term
accuracy, but the inserted pulses make the clock edge stream non-uniform. For
IMUTag, the clock edge stream also feeds the STM32 RTC and the LSM6 trigger
chain, so a smoother strategy is preferable:

```text
RV-3028 CLKOUT:      32.768 kHz direct, uncompensated
STM32 RTC:           1024 Hz subseconds first, STM32 smooth-calibrated
LSM6 trigger:        raw LSE-derived clock, LPTIM prescaled to 1024 Hz
Log metadata:        store factory EEOffset
Host decoder:        apply smooth linear correction
```

The RV-3028 application manual describes the `32.768 kHz` CLKOUT selection as
the direct crystal oscillator output, while the lower divided frequencies
(`8192 Hz`, `1024 Hz`, `64 Hz`, `32 Hz`, and `1 Hz`) can be affected by
compensation pulses. Therefore, the preferred implementation is not to erase or
rewrite the factory offset. Leave the factory calibration in EEPROM, read it,
store it with the log, and select the direct 32.768 kHz output so runtime timing
is smooth.

The preferred STM32U375 implementation separates the two consumers of that raw
clock:

```text
RV-3028 direct 32.768 kHz CLKOUT
        |
        +-- LSE-derived LPTIM trigger: raw, uniform sampling clock
        |
        +-- STM32 RTC calendar: smooth-calibrated with RTC_CALR
```

The LSM6DSV16X trigger is downstream of LSE/LPTIM, not downstream of the RTC
calendar correction. That means STM32 `RTC_CALR` correction can keep real-time
calendar reads accurate without adding correction-step jitter to the sampling
trigger.

With this split, the two timing streams meet only at segment anchors such as
collection start, restart recovery, or an explicit resync/discontinuity. Within
a segment, samples stay on the reconstructed raw-clock sample grid. At an
anchor, the host ties that grid to the corrected RTC wall-clock time.

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

## Firmware Changes

### RTC Configuration

Files:

- `embedded/tags/families/IMUTag/cfg/mcuconf.h`
- `embedded/tags/IMUTagNand/cfg/mcuconf.h`

The 1024 Hz reconstruction work itself does not require an RTC configuration
change. Active IMUTag variants can continue to use the existing 1024 Hz divided
RV-3028 reference until the firmware-only power prototype or production
smooth-clock phase changes the clock path:

```c
#define STM32_RTC_PRESA_VALUE 1
#define STM32_RTC_PRESS_VALUE 1024
```

The firmware-only power prototype and later production smooth-clock phase
should select the RV-3028 direct 32.768 kHz CLKOUT while preserving a 1024 Hz
RTC subsecond counter:

```c
#define STM32_RTC_PRESA_VALUE 32
#define STM32_RTC_PRESS_VALUE 1024
```

Files:

- `embedded/tags/common/rtc/inc/rtc_api.h`
- `embedded/tags/common/rtc/src/rtc_rv3028.c`
- `embedded/tags/common/rtc/src/hal_rtc_lld.c`

The RV-3028 CLKOUT selection already maps a prescaler product of 32768 to
`RV3028_CLKOUT_VAL = 0`, selecting the direct 32.768 kHz output. Add comments
or static checks in the smooth-clock phase so future changes do not accidentally
select a compensated divided CLKOUT frequency for IMUTag.

Add a small STM32 RTC calibration API close to the RTC low-level code, for
example:

```c
bool stm32RtcApplySmoothCalibration(bool calp,
                                    uint16_t calm,
                                    uint32_t window_seconds);
```

The implementation must wait for `RECALPF` to clear before updating `RTC_CALR`
and should keep the register programming out of the sample/log hot path.

### Timekeeping Helpers

Files:

- `embedded/tags/common/core/inc/timekeeping.h`
- `embedded/tags/common/core/src/time.c`
- target RTC low-level files only if a shared raw-read helper cannot be written
  safely in `time.c`:
  - `embedded/tags/common/rtc/src/hal_rtc_lld.c`
  - `embedded/tags/IMUTagNand/src/hal_rtc_lld.c`

Add a helper that reads epoch seconds plus raw RTC subsecond ticks before the
HAL normalizes the counter to integer milliseconds. The helper should preserve
the existing double-read rollover protection used by the RTC HAL.

Proposed API:

```c
int32_t GetTimeUnixSecRawSubsecond(uint32_t *subsecond_ticks,
                                   uint32_t *subsecond_hz);
uint32_t ScaleSubsecondTicks(uint32_t ticks,
                             uint32_t from_hz,
                             uint32_t to_hz);
```

Keep `GetTimeUnixSec(uint32_t *millis)` ABI-compatible. Existing callers should
continue to get the legacy public representation. During the reconstruction
phase, IMUTag log code can still store the existing 1024 Hz page ticks; the
helper becomes necessary when the smooth-clock phase needs raw RTC ticks without
millisecond normalization.

### IMUTag Log Format

File:

- `include/imutag_log_format.h`

Add the configurable macros described above, defaulting
`IMUTAG_LOG_SUBSECOND_HZ` to `1024u`. Update comments to say "subsecond ticks"
rather than "milliseconds" where the statement depends on the selected
timebase. The active plan keeps the existing mask and flag values through the
1024 Hz default.

Keep `t_ImuTagPageHeader` at 8 bytes:

```c
typedef struct {
    int32_t epoch;
    uint16_t millis;
    int16_t rawtemp;
} t_ImuTagPageHeader;
```

The field remains named `millis` to avoid a layout change. In the active plan,
its low bits are still 1024 Hz log subsecond ticks.

### IMUTag RUN-State Page Anchors

File:

- `embedded/tags/families/IMUTag/src/state_run.c`

During the reconstruction phase, keep page-anchor capture in the existing
1024 Hz domain and make the host reconstruction treat ordinary page headers as
checkpoints rather than the sample clock. In the later smooth-clock phase,
change page-anchor capture from the main-loop `timestamp_millis` value to the
new raw-log timestamp helper. The page-start path should store:

```text
current_page_header.epoch = raw_epoch_seconds
current_page_header.millis = raw_subsecond_ticks & IMUTAG_LOG_SUBSECOND_MASK
```

The RUN-state comments should make the split explicit:

- `timestamp` / `timestamp_millis` remain public/status time.
- `current_page_header` uses `IMUTAG_LOG_SUBSECOND_HZ`.

### IMUTag Data Download ACK

File:

- `embedded/tags/families/IMUTag/src/datalog.c`

Update all masks and flag extraction to use `IMUTAG_LOG_SUBSECOND_MASK`.

The existing `millisecond` protobuf member continues to carry the packed
subsecond/flags field. Its active IMUTag meaning remains low 10 bits at
1024 Hz.

### LSM6DSV16X Trigger Timer

File:

- `embedded/tags/families/IMUTag/src/devices.c`

The reconstruction phase requires no LSM6DSV16X trigger change. In the
firmware-only power prototype and later production smooth-clock phase, treat
the divider received from the common LSM6DSV16X driver as the same
1024 Hz-domain divider it is today. Program only the LPTIM prescaler to divide
the raw 32.768 kHz source down to a 1024 Hz timer count domain before ARR/CMP:

```c
tagImuTagSetTriggerPrescaler(IMUTAG_IMU_TRIGGER_LPTIM_PRESCALER_DIV);
IMUTAG_IMU_TRIGGER_LPTIM->ARR = divider - 1U;
tagImuTagSetTriggerCompare(divider / 2U);
```

Keep logging the prescaler and logical divider during bring-up:

```text
IMUTag trigger: input 32768 Hz, prescaler 32, logical divider 128
```

Files:

- `embedded/tags/common/sensors/imu/lsm6dsv16x.c`
- `embedded/tags/common/sensors/imu/lsm6dsv16x.h`
- `embedded/tags/common/sensors/imu/lsm6dsv16x_regs.h`
- `embedded/tags/common/sensors/imu/design/assumptions.md`

No behavior change is required in the common LSM6 driver if the IMUTag board
layer prescales the LPTIM counter domain back to 1024 Hz. In the smooth-clock
phase, update comments to say the table uses the logical 1024 Hz trigger
domain, not necessarily the physical LPTIM input clock.

## Protobuf and Nanopb Changes

File:

- `proto/tagdata.proto`

No schema field is required. The reconstruction phase does not change protobuf
semantics; update comments only to document that `millisecond` is an IMUTag
packed subsecond field whose frequency is defined by the firmware/log contract:

```proto
message IMUTagRawLog {
  int32 epoch = 1;
  // Packed t_DataHeader.millis: low bits are IMUTag log subsecond ticks,
  // upper bits are flags.
  int32 millisecond = 2;
  float temperature = 3;
  bytes samples = 4;
}
```

Consider adding the same field to legacy `IMUTagLog` only if that message is
revived. Current host code rejects legacy decoded-block IMUTag logs.

Files:

- `embedded/proto-c/imutag-proto-c/tagdata.override.options`
- generated protobuf/nanopb outputs under the normal build-generated locations

No nanopb sizing changes are expected because the message shape does not
change. Regenerate protobuf outputs through the repository's normal proto build
path only if generated comments or descriptors are committed in this repository.

Compatibility boundary:

- IMUTag firmware, host import code, and SQLite output can move together during
  this development phase.
- No backward compatibility is required for older IMUTag firmware logs or older
  IMUTag SQLite files.
- Existing non-IMUTag protobuf timestamp semantics do not change.

## Host Software Changes

### SQLite Log Decoder

File:

- `host/libraries/tagcore/sqlitelog/imutag.cc`

The reconstruction phase keeps the fixed decoder constant at 1024 Hz but makes
the reconstruction contract explicit:

```c++
constexpr uint32_t kImuHeaderSubsecondTicksPerSecond = 1024;
```

The decoder should:

- build continuous timing segments from collection start, restart/recovery
  resync, and explicit storage discontinuity anchors;
- compute each sample time from segment start plus accumulated sample count and
  configured ODR;
- advance the sample index across missing pages when the missing-page count is
  known from page sequence/storage metadata;
- use ordinary page-header timestamps only for diagnostics and anchor-error
  reporting.

Decode with `IMUTAG_LOG_SUBSECOND_MASK` rather than a literal `0x03ff` mask.
Convert subsecond ticks to rounded milliseconds for the existing
`ImuHeader.Millisecond` column, and add raw subsecond columns if exact anchor
metadata should be preserved after import.

Recommended SQLite behavior:

- Keep `Millisecond` for existing sensorViz metadata.
- Add `SubsecondTicks` and `SubsecondHz` to `ImuHeader` when exact anchor
  metadata needs to survive import.
- Store every major segment header used by reconstruction, even if the importer
  also writes fully reconstructed sample times.

### SQLite Schema Metadata

File:

- `host/libraries/tagcore/sqlitelog/schema.cc`

Update the `ImuHeader` table definition and comments:

```text
Epoch           integer seconds
Millisecond     rounded millisecond for legacy UI metadata
SubsecondTicks  raw packed-log subsecond ticks
SubsecondHz     tick frequency for SubsecondTicks
Flags           unpacked IMUTag header flags
```

No IMUTag SQLite migration path is required during this development phase. The
writer can create fresh output files using the new schema once the schema
changes are implemented.

Add a raw-header/segment-anchor table so the SQLite file can be used to
deconstruct the imported data back into its timing model. The exact name can
follow existing schema conventions, but the content should be equivalent to:

```text
ImuSegmentHeader
  SegmentId                 monotonically increasing reconstruction segment
  PageIndex                 downloaded page index or storage page number
  HeaderKind                collection_start, restart_resync, storage_skip,
                            diagnostic_page, or end
  RawEpoch                  raw header epoch seconds
  RawSubsecondTicks         raw packed-log subsecond ticks
  RawSubsecondHz            tick frequency for RawSubsecondTicks
  HeaderFlags               packed/unpacked IMUTag page flags
  FirstSampleIndex          reconstructed global sample index at this anchor
  SamplesBeforeAnchor       cumulative samples before this segment
  MissingPagesBeforeAnchor  known skipped/missing pages before this anchor
  ConfiguredOdrHz           ODR used for this segment
  RtcClockMode              compensated_clkout, direct_clkout,
                            or stm32_smooth_calibrated
  CorrectionPpm             ppm used to map raw sample elapsed time to wall time
```

For ordinary pages, storing every page header is useful for diagnostics but not
required for reconstruction. For major headers and segment boundaries, storage
is required: collection start, restart recovery, storage discontinuity, and any
explicit resync marker. These rows let a developer recompute the sample timeline
from raw samples, ODR, page sizes, missing-page counts, and segment anchors
without trusting the first importer's derived timestamps.

### SensorViz Loader

Files:

- `host/applications/sensorviz/sqlite_loader.cpp`
- `host/applications/sensorviz/sensorstream.h`
- `host/applications/sensorviz/README.md`

SensorViz can continue using `Epoch * 1000 + Millisecond` for collection-start
and event display. Sample plots should continue to use reconstructed elapsed
sample timing from the SQLite writer. If exact log-anchor metadata is exposed
later, load `SubsecondTicks/SubsecondHz` when present.

Remove any IMUTag-specific `0x03ff` mask from sensorViz once SQLite stores
already-decoded columns.

### Download Transport

Files:

- `host/applications/qtmon/abstractdownload.cpp`
- `host/commandline/dwnld.cc`

No behavior change is expected. The downloader counts pages and passes ACKs to
the writer. Update comments only if needed.

## Documentation Changes

Files:

- `proto/tagdata.proto`: update field comments to explain the timebase field.
- `include/imutag_log_format.h`: document the explicit 1024 Hz subsecond domain
  and named masks/flags.
- `embedded/tags/families/IMUTag/README.md`: link this plan.
- `embedded/tags/families/IMUTag/design/internal-header-checkpoints.md`: update
  the header-layout section after the reconstruction phase so it refers to the
  explicit 1024 Hz reconstruction contract.
- `host/libraries/tagcore/sqlitelog/schema.cc`: update table comments.
- `host/applications/sensorviz/README.md`: update elapsed-log metadata wording
  if raw subsecond anchor metadata becomes visible.

## Implementation Phases

### Phase 1: Firmware-Only Power Impact Prototype

This phase intentionally avoids metadata, downloader, protobuf, SQLite, and
host reconstruction changes. Its purpose is to measure the runtime and sleep
current impact of moving the clock/correction work into the STM32U375 while
keeping the LSM6 trigger output frequencies unchanged.

1. Change active STM32U3 IMUTag `mcuconf.h` RTC values to
   `PRESA/PRESS = 32/1024` so the RV-3028 CLKOUT selection becomes the direct
   32.768 kHz output while the STM32 RTC subsecond counter remains 1024 Hz.
2. Read the RV-3028 factory `EEOffset` at RTC initialization and program STM32
   `RTC_CALR` with the equivalent smooth-calibration value.
3. Set only the LPTIM prescaler in
   `embedded/tags/families/IMUTag/src/devices.c` so the LPTIM counter domain
   remains 1024 Hz; keep existing ARR/CMP divider values unchanged.
4. Do not add correction anchors, log metadata, SQLite columns, protobuf
   comments, downloader behavior, or host reconstruction behavior in this
   phase.
5. Measure power against the current compensated-CLKOUT firmware in the same
   operating states: idle/monitor, recording at representative ODRs, sleep, and
   restart/recovery if practical.
6. Scope RV-3028 CLKOUT and LSM6 trigger output to confirm the input clock is
   direct 32.768 kHz and the sensor trigger rates are unchanged.

Expected behavior: the tag uses a jitter-free raw sampling clock and an
STM32-corrected RTC calendar, with no log-format or downloader change. The
decision gate is whether the RTC calibration path and LPTIM prescaler change
have acceptable power cost.

### Phase 2: 1024 Hz Timing Reconstruction

1. Keep IMUTag firmware page headers in the existing 1024 Hz packed format:
   low ten bits are subsecond ticks and existing flags remain at
   `0x0400`, `0x0800`, and `0x1000`.
2. Add named IMUTag log constants in `include/imutag_log_format.h`, defaulting
   `IMUTAG_LOG_SUBSECOND_HZ` to `1024u`, so the current contract is explicit.
3. Update `host/libraries/tagcore/sqlitelog/imutag.cc` so continuous data timing
   is reconstructed from segment start, configured ODR, and accumulated sample
   index.
4. Treat ordinary page-header timestamps as diagnostics; use page timestamps as
   anchors only for collection start, restart/recovery resync, and explicit
   storage discontinuities.
5. When missing-page count is known, advance the reconstructed sample index
   across the missing samples instead of re-anchoring the next page.
6. Store the major reconstruction headers/anchors in SQLite so the timing model
   can be deconstructed or recomputed from the database.
7. Add host fixtures covering continuous pages, missed pages, restart/resync,
   and anchor-error reporting.

Expected behavior: IMUTag logs continue using the 1024 Hz format, while
imported sample timing no longer follows ordinary per-page RTC jitter.

### Phase 3: Correction Factor Metadata

1. Add RV-3028 `EEOffset` read support in the RTC driver.
2. Persist the correction factor and RTC correction mode after RTC
   initialization/configuration.
3. Include `rv3028_eeoffset_steps`, derived correction ppm or ppb, RTC clock
   mode, STM32 RTC smooth-calibration state, and applied `CALP/CALM` values in
   downloaded metadata or SQLite output.
4. Do not add additional firmware scheduling, status timestamp, or raw-log
   behavior beyond any Phase 1 prototype changes already under test.
5. Teach host import/export code to record the timing domain for each header and
   apply the correction factor only to raw LSE/LPTIM sample elapsed time.

Expected behavior: host software has enough metadata to explain and apply a
smooth calibration correction, but the hot logging path is unchanged.

### Phase 4: Productionize Smooth Clock and RTC Calibration

1. Keep the Phase 1 `PRESA/PRESS = 32/1024` clock configuration if power
   measurements are acceptable.
2. Add static checks/comments in `rtc_api.h` and `rtc_rv3028.c` documenting that
   IMUTag smooth-clock builds require `TAG_RTC_REFERENCE_HZ = 32768`.
3. Store the applied STM32 `CALP/CALM` values in metadata once the metadata path
   from Phase 3 exists.
4. Promote the Phase 1 LPTIM prescaler change from prototype to the normal
   IMUTag trigger setup path; keep ARR/CMP divider values in the existing
   1024 Hz domain.
5. Verify physical LSM6 trigger output frequencies match the old values for
   each configured ODR.
6. Enable host smooth correction for reconstructed sample elapsed time using
   the stored `EEOffset` correction factor, while treating RTC event headers
   as already corrected when STM32 RTC smooth calibration was active.

Expected behavior: the sensor trigger and log timestamps keep their current
logical rates, the RTC calendar stays calibrated for operator-facing real time,
and the LSM trigger clock edge stream no longer contains RV-3028
compensation-pulse jitter.

## Verification Checklist

Firmware:

- Build each affected firmware target.
- For the firmware-only prototype, compare current draw against the existing
  compensated-CLKOUT firmware in idle/monitor, recording at representative
  ODRs, sleep, and restart/recovery if practical.
- For the firmware-only prototype and reconstruction phases, confirm the
  page-header binary layout is unchanged:
  `0x03ff` subsecond mask and existing flag bits.
- For the smooth-clock phase, confirm `RV3028_CLKOUT_VAL` resolves to the
  direct 32.768 kHz output.
- Scope RV-3028 CLKOUT and LSM6 trigger output after any RTC/reference or LPTIM
  prescaler change.
- Verify LSM6 output rates at 50, 100, 200, 400, 800, and 1600 Hz if supported
  after any LPTIM prescaler change.
- Verify page-header subsecond values span `0..1023`.
- Verify non-log status/protobuf time is still 1024-compatible.
- Verify the RV-3028 correction factor and applied STM32 `CALP/CALM` values are
  captured in metadata once that phase is implemented.
- Exercise second rollover during page-anchor capture.
- Exercise monitor attach/restart recovery and confirm resync flags survive
  with the selected flag bit positions.

Host:

- Add unit or fixture tests for `IMUTagRawLog` reconstruction with 1024 Hz
  packed timestamps.
- Verify missing pages advance reconstructed elapsed sample time without forcing
  a new wall-clock anchor.
- Verify restart/recovery resync uses the next valid page timestamp as a new
  segment anchor.
- Verify SQLite stores the major raw headers/segment anchors needed to
  reconstruct the timing model without re-reading the original device log.
- Verify SQLite `ImuHeader` stores rounded millisecond metadata and raw
  subsecond metadata if those columns are added.
- Verify host import applies the RV-3028 correction only to raw-clock sample
  timing domains, and preserves calibrated RTC event timestamps without
  double-correction.
- Verify sensorViz still plots elapsed IMU samples from `ElapsedUs`.

Cross-tag isolation:

- Confirm non-IMUTag tags build without defining `IMUTAG_LOG_SUBSECOND_HZ`.
- Confirm non-IMUTag log decoders and timestamp comments are unchanged.
- Confirm status/public time remains in the 1024 Hz-compatible domain.

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
