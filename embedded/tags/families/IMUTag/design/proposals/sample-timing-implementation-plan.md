---
type: proposal
status: historical
summary: The spent 2026-08 file-by-file change list, four implementation phases and verification checklist for IMUTag jitter-free sample timing.
---

# IMUTag Sample-Timing Implementation Plan (2026-08)

Cut verbatim from [`../sample-timing.md`](../sample-timing.md), where the plan
was written on 2026-08-09. The plan has largely been carried out: the
IMUTagNand and IMUTagNandBmp581 targets select the direct 32.768 kHz RV-3028
CLKOUT (`STM32_RTC_PRESA_VALUE 32`, `TAG_RTC_REQUIRE_DIRECT_RV3028_CLKOUT`),
enable `IMUTAG_USE_STM32_RTC_SMOOTH_CALIBRATION`, prescale the LSM6 trigger
LPTIM back to the 1024 Hz domain, and the host decoder reconstructs segments
from resync anchors. Some items were not built as written (for example the
named `IMUTAG_LOG_SUBSECOND_HZ` macros). Kept for the record; describe the
system from the code and `sample-timing.md`, not from this plan.

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
