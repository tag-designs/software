# ChibiOS STM32U3 RTC Findings

Date: 2026-07-05

This note summarizes two RTC issues found while bringing up an STM32U375KGU6
target using ChibiOS with `HAL_USE_RTC == TRUE`. The target uses an external
RV3028 RTC that feeds the STM32 LSE input in bypass mode.

## Environment

- ChibiOS tree: local `ChibiOS/` submodule in this repository
- MCU family: STM32U3
- Tested part: STM32U375KGU6
- RTC peripheral: STM32 RTCv3 driver
- Relevant ST header: `ChibiOS/os/common/ext/ST/STM32U3xx/stm32u375xx.h`

## Issue 1: STM32U3 HAL Enables RTCAPB On APB3, But STM32U375 Defines It On APB1ENR1

### Symptom

The RTC calendar registers could be read, but attempts to enter RTC
initialization mode failed. In practice, setting `RTC_ICSR_INIT` never produced
`RTC_ICSR_INITF`, so `rtcSetTime()` did not update `TR`/`DR`. The RTC stayed at
its reset/default calendar until project code explicitly enabled the RTC APB
interface clock.

### ChibiOS Code

In `ChibiOS/os/hal/ports/STM32/STM32U3xx/hal_lld.c`:

```c
/* RTC APB clock enable.*/
#if (HAL_USE_RTC == TRUE) && defined(RCC_APB3ENR_RTCAPBEN)
  rccEnableAPB3(RCC_APB3ENR_RTCAPBEN, true);
#endif
```

### Header Evidence

In `ChibiOS/os/common/ext/ST/STM32U3xx/stm32u375xx.h`, the RTC APB clock is
defined on APB1ENR1, not APB3ENR:

```c
#define RTC_BASE_NS (APB1PERIPH_BASE_NS + 0x00007800UL)
#define RTC_BASE_S  (APB1PERIPH_BASE_S  + 0x00007800UL)

#define RCC_APB1ENR1_RTCAPBEN_Pos (30UL)
#define RCC_APB1ENR1_RTCAPBEN_Msk (0x1UL << RCC_APB1ENR1_RTCAPBEN_Pos)
#define RCC_APB1ENR1_RTCAPBEN     RCC_APB1ENR1_RTCAPBEN_Msk
```

No `RCC_APB3ENR_RTCAPBEN` symbol exists for this header, so the ChibiOS
conditional compiles out and never enables the RTC APB interface clock.

### Local Confirmation

With the stock U3 HAL path, runtime diagnostics showed:

```text
init=0 icsr=0x00000000 bdcr=0x00008983 apb3=0x00000000
```

After enabling `RCC->APB1ENR1 |= RCC_APB1ENR1_RTCAPBEN` before RTC init-mode
entry:

```text
init=1 icsr=0x00000017 bdcr=0x00008983 apb1=0x40000001 dr=0x00464705
```

The calendar then updated correctly and live monitor status reported current
time instead of the reset/default RTC date.

### Suggested Fix

Use APB1ENR1 for STM32U3 parts that define `RCC_APB1ENR1_RTCAPBEN`:

```c
#if (HAL_USE_RTC == TRUE) && defined(RCC_APB1ENR1_RTCAPBEN)
  rccEnableAPB1R1(RCC_APB1ENR1_RTCAPBEN, true);
#elif (HAL_USE_RTC == TRUE) && defined(RCC_APB3ENR_RTCAPBEN)
  rccEnableAPB3(RCC_APB3ENR_RTCAPBEN, true);
#endif
```

This keeps compatibility if other STM32U3 variants or headers place RTCAPB
elsewhere.

## Issue 2: RTCv3 Date Decode Uses Time-Register Month Bit Offsets

### Symptom

The STM32 RTCv3 driver decodes the month from the date register using offsets
from the time register. This can decode `RTCDateTime.month` incorrectly.

### ChibiOS Code

In `ChibiOS/os/hal/ports/STM32/LLD/RTCv3/hal_rtc_lld.c`:

```c
timespec->month = (((dr >> RTC_TR_MNT_OFFSET) & 1) * 10) +
                   ((dr >> RTC_TR_MNU_OFFSET) & 15);
```

`dr` is the RTC date register value, so the month fields should use date-register
offsets.

### Suggested Fix

```c
timespec->month = (((dr >> RTC_DR_MT_OFFSET) & 1) * 10) +
                   ((dr >> RTC_DR_MU_OFFSET) & 15);
```

This is the change applied in the local STM32U375 RTC override.

## Non-ChibiOS Issue Found During Bring-Up

One project-local RV3028 driver bug also affected this bring-up, but it is not a
ChibiOS defect. The local driver checked:

```c
tmp == (0xC0 & RV3028_CLKOUT_VAL)
```

where it should check:

```c
tmp == (0xC0 | RV3028_CLKOUT_VAL)
```

This caused the driver to reject a valid RV3028 `CLKOUT` configuration after
writing it.

## Notes

The tested board feeds the STM32 LSE input from an external RTC clock output
rather than a 32.768 kHz crystal. Long, unbounded LSE/RTC initialization waits
are therefore especially visible during debug attach. This is separate from the
two ChibiOS issues above: the APB1 RTCAPB gate is required for writes to enter
RTC init mode, and the month decode issue is a pure RTCv3 field-offset bug.
