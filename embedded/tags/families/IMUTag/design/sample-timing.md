---
type: design
status: current
summary: How IMUTag sample times are produced -- a jitter-free raw 32.768 kHz sampling clock, a smooth-calibrated STM32 RTC, 1024 Hz page anchors with resync flags, and host reconstruction from sample count.
last-verified: a87fc84a
---

# IMUTag Sample Timing

IMU samples are timed by **count, not by timestamp**. The LSM6DSV16X is
triggered from the raw, uncompensated 32.768 kHz RV-3028 output, so its sample
grid has no correction-pulse jitter; the host computes each sample's time as
`segment start + sample index / ODR`, scaled once by the RV-3028 factory clock
correction. Page-header timestamps are 1024 Hz checkpoints and are used as
anchors only at collection start and at explicitly flagged discontinuities.

This applies to `IMUTagNand` and `IMUTagNandBmp581`. Why the factory
correction is applied in the STM32 RTC rather than in the RV-3028 output is
recorded in
[0002-imutag-rtc-smooth-calibration-over-compensated-clkout.md](../../../../../docs/decisions/0002-imutag-rtc-smooth-calibration-over-compensated-clkout.md).
The plan this was built from is
[proposals/sample-timing-implementation-plan.md](proposals/sample-timing-implementation-plan.md).

## Clock path

```text
RV-3028 CLKOUT, direct 32.768 kHz (uncompensated)
        |
        +-- STM32 LSE --> LPTIM, prescaled /32 to a 1024 Hz count
        |                 --> LSM6DSV16X external ODR trigger (sample clock)
        |
        +-- STM32 RTC calendar, PRESA/PRESS = 32/1024,
            smooth-calibrated from the RV-3028 EEOffset via RTC_CALR
            --> page-header and event timestamps (already corrected)
```

| Setting | Where | Value |
| --- | --- | --- |
| `STM32_RTC_PRESA_VALUE` / `STM32_RTC_PRESS_VALUE` | target `cfg/mcuconf.h` | 32 / 1024 |
| `TAG_RTC_REQUIRE_DIRECT_RV3028_CLKOUT` | target `inc/custom.h` | 1: build fails unless the prescaler product selects the direct 32.768 kHz CLKOUT |
| `IMUTAG_USE_STM32_RTC_SMOOTH_CALIBRATION` | target `inc/custom.h` | 1 (driver default 0) |
| `TAG_RTC_REFERENCE_HZ` | `common/rtc/inc/rtc_api.h` | `PRESA * PRESS` = 32768 |
| `IMUTAG_IMU_TRIGGER_LOGICAL_HZ` | `families/IMUTag/src/devices.c` | 1024 |

The family default `families/IMUTag/cfg/mcuconf.h` still sets `PRESA = 1`
(the 1024 Hz compensated CLKOUT); both active targets override it.

**Trigger.** The common LSM6DSV16X driver computes its ARR/CMP dividers in a
logical 1024 Hz domain. `devices.c` sets only the LPTIM prescaler, to
`TAG_RTC_REFERENCE_HZ / IMUTAG_IMU_TRIGGER_LOGICAL_HZ` (32), so the physical
trigger rate for every ODR is the same as it would be from a 1024 Hz input. A
reference frequency that is not an integer multiple of 1024 Hz is a build
error.

**RTC calibration.** RTC initialisation in `common/rtc/src/rtc_rv3028.c` reads
the 9-bit signed factory `EEOffset` (register `0x36` as bits 8:1, bit 7 of
register `0x37` as bit 0).
One step is `1e6 / (16384 * 64)` ≈ 0.9537 ppm, so the range is about
+243.2 / -244.1 ppm. `SetTimeUnixSec()` (`common/core/src/time.c`) calls
`tagRtcApplyClockCorrection()` after every clock set, which maps the step count
into `RTC_CALR` (`steps < 0`: `CALM = -steps`; `steps > 0`: `CALP = 1`,
`CALM = 512 - steps`) after waiting for `RECALPF`. On a `TAG_RTC_STM32U3_COMPAT`
build a failed correction makes the clock set fail.

## Log contract

The external page header (`t_DataHeader`, `include/imutag_log_format.h`) holds
`epoch` seconds and a 16-bit `millis` field. Despite the name, its low ten bits
(`IMUTAG_HEADER_MILLIS_MASK`, `0x03ff`) are **1/1024 s ticks** from the
calibrated RTC; the upper bits are flags:

| Flag | Bit | Set by `families/IMUTag/src/state_run.c` when |
| --- | --- | --- |
| `IMUTAG_HEADER_RESYNC` | `0x0400` | a discontinuity the host must re-anchor at: a storage skip, or a collection restart after a monitor connect-under-reset while RUNNING |
| `IMUTAG_HEADER_RESYNC_STORAGE_SKIP` | `0x0800` | with `RESYNC`, when the storage layer skipped pages |
| `IMUTAG_HEADER_RESTART_RECOVERY` | `0x1000` | with `RESYNC` on that restart; `restoreLog()` (`datalog.c`) also queues it alone after a reset in CONFIGURED, RUNNING or HIBERNATING |

The host re-anchors only on `IMUTAG_HEADER_RESYNC`; the other two bits name the
cause.

A pending flag is kept in `pState->checkpoint_flags_pending` (backup domain)
until the checkpoint that carries it is written, so a reset in between does not
lose the discontinuity. How checkpoints are placed in internal flash is in
[internal-header-checkpoints.md](internal-header-checkpoints.md).

The RV-3028 correction reaches the host as `TagInfo.ppm_clock_error`
(`proto/tagdata.proto`). `monitor.c` reports the offset recorded in the stored
session facts at start when there is one, so a download and an offline rebuild
see the same value; otherwise it reads the RV-3028 live. The applied
`CALP/CALM` values are not reported.

## Host reconstruction

`host/libraries/tagcore/sqlitelog/imutag.cc`, per downloaded page:

- The **first** page anchors the collection; its wall-clock time is the zero of
  `ElapsedUs`.
- A page with `IMUTAG_HEADER_RESYNC` starts a new segment. Its start is the
  header time relative to the anchor, or the end of the previous segment if
  that is later, so `ElapsedUs` never steps backwards.
- Every other page continues the current segment; its header time is not used.
- Within a segment, sample `i` has `RawElapsedUs = i * P` (restarting at zero
  in each segment) and
  `ElapsedUs = segment start + round(i * P * (1 + ppm / 1e6))`, where
  `P = 1000000 / ODR` in integer microseconds (truncated when the ODR does not
  divide 1 000 000). The
  correction is computed from the index, not accumulated. RTC-derived header
  and event times are already corrected and are not scaled again.
- A wholly erased superframe ends the page.

SQLite keeps what is needed to redo this: every page header in `ImuHeader`
(`Epoch`, rounded `Millisecond`, raw `SubsecondTicks`, `SubsecondHz` = 1024,
`Flags`), one `ImuSegment` row per anchor (`Event` = `COLLECTION_START`,
`RESYNC`, `RESYNC_STORAGE_SKIP` or `RESTART_RECOVERY`, `FirstSampleIndex`,
`ConfiguredOdrHz`, `CorrectionPpm`), and an `ImuEvent` row per resync. Use
`ElapsedUs`, not `RawElapsedUs`, as a monotonic time column.

## Accuracy

Within a segment, timing is the sample grid: free of RV-3028 compensation-pulse
jitter, and accurate to the residual error of the factory correction. A segment
anchor is quantised to the 1024 Hz header tick, about ±488 us, and the host
then rounds that tick to the nearest millisecond (`page_epoch_ms` in
`imutag.cc`) before using it, adding up to ±500 us more. Reconstruction
does not remove RTC set-time uncertainty, wake or FIFO latency, or jitter
inside the sensor.

Moving page anchors to 8192 Hz would cut the header quantisation to about
±61 us without changing in-segment timing, but the anchor would reach that only
if the host also stopped rounding it to milliseconds; it was considered and not
built (see the
plan's *Future Consideration*). Open items are in [`../TODO.md`](../TODO.md).
