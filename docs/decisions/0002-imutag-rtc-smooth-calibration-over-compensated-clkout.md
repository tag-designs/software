---
type: decision
status: accepted
summary: IMUTag samples from the direct, uncompensated 32.768 kHz RV-3028 CLKOUT and moves the factory clock correction into STM32 RTC smooth calibration, instead of using the RV-3028's compensated divided CLKOUT.
---

# 0002. IMUTag: STM32 RTC smooth calibration instead of compensated RV-3028 CLKOUT

Date: 2026-08-09

The text below is cut verbatim from the *Smooth RV-3028 Compensation Plan*
section of
[`embedded/tags/families/IMUTag/design/sample-timing.md`](../../embedded/tags/families/IMUTag/design/sample-timing.md),
written on 2026-08-09. The choice was built: IMUTagNand and IMUTagNandBmp581
define `IMUTAG_USE_STM32_RTC_SMOOTH_CALIBRATION 1` and
`TAG_RTC_REQUIRE_DIRECT_RV3028_CLKOUT 1` in `inc/custom.h`, and
`embedded/tags/common/rtc/src/rtc_rv3028.c` maps the RV-3028 `EEOffset` into
`RTC_CALR`.

## Context, Decision and Alternatives considered

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

## Consequences

See [`sample-timing.md`](../../embedded/tags/families/IMUTag/design/sample-timing.md)
for the resulting timing model.
