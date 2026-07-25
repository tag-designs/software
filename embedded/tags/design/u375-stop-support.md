# STM32U375 Stop-Mode Support Notes

This note scopes the work needed to make the U375 IMUTag variants spend idle
collection time in Stop mode without losing sensor wakeups or ChibiOS timing.
It is intentionally conservative: register names and wake behavior must be
checked against the STM32U375 reference manual and the exact ChibiOS STM32U3
port before implementation.

## Goals

- Let the acquisition thread block on the LSM6DSV16X FIFO watermark interrupt
  instead of polling.
- Let the idle thread enter Stop1 while all runnable threads are blocked.
- Keep ChibiOS timeouts meaningful across Stop1, or explicitly avoid relying on
  timeouts while the system timer is stopped.
- Preserve the current U3 clock plan: MSIS for SYSCLK and MSIK for SPI/kernel
  peripherals.
- Treat the board LSE as a 1024 Hz source. Do not assume the usual 32768 Hz
  watch crystal.
- Treat SRAM1 run-time power-down as a later optimization, separate from Stop1.

## Current U3 Assumptions

The current `IMUTagU3bmm350` configuration is the useful baseline:

- `STM32_LSE_ENABLED TRUE`
- `STM32_LSI_ENABLED FALSE`
- `STM32_LPTIM1SEL RCC_CCIPR3_LPTIM1SEL_LSE`
- `STM32_LPTIM2SEL RCC_CCIPR1_LPTIM2SEL_LSE`
- `STM32_LPTIM34SEL RCC_CCIPR3_LPTIM34SEL_LSE`
- `STM32_ST_USE_TIMER 2`
- `STM32_STOPWUCK RCC_CFGR1_STOPWUCK_MSIS`
- `STM32_STOPKERWUCK RCC_CFGR1_STOPKERWUCK_MSIK`
- `USE_STOP1 1`
- `TAG_STOP1_WAKE_USES_INTERRUPT 1`

That means the existing ChibiOS system timer is still a regular timer, not a
low-power timer. A regular timer can work for normal run/sleep, but it should
not be assumed to keep time or wake the core from Stop1.

The non-negotiable clock constraint is that LSE is 1024 Hz on this hardware.
That is suitable for RTC-style calendar/timekeeping, but it is a poor source
for the ChibiOS system timer if we want normal timeout behavior and low compare
latency. A Stop1-capable ChibiOS ST implementation should therefore move the
chosen LPTIM34 clock source to LSI and enable LSI for that target.

## Recommended Work Plan

### 1. Make the LSM Interrupt the Collection Wait Primitive

Use a binary semaphore or event source signaled from the LSM6 EXTI callback.
The data-collection thread should wait on that primitive, then drain the FIFO
when the watermark is available.

Minimal shape:

```c
static binary_semaphore_t imu_wtm_bsem;

static void imu_wtm_cb(void *arg)
{
  (void)arg;
  chSysLockFromISR();
  chBSemSignalI(&imu_wtm_bsem);
  chSysUnlockFromISR();
}

void imu_wtm_wait_init(void)
{
  chBSemObjectInit(&imu_wtm_bsem, true);
  palSetLineCallback(LINE_WKUP1, imu_wtm_cb, NULL);
  palEnableLineEvent(LINE_WKUP1, PAL_EVENT_MODE_RISING_EDGE);
}

msg_t imu_wtm_wait(sysinterval_t timeout)
{
  return chBSemWaitTimeout(&imu_wtm_bsem, timeout);
}
```

Use `TIME_INFINITE` if the acquisition mode only needs sensor-driven wakeups.
Use a finite timeout only after the system timer source is proven to run across
Stop1.

### 2. Enter Stop1 Only From a Central Idle Path

Do not scatter raw `__WFI()` snippets through application code. The idle path
should be the only place that sets deep-sleep state, enters Stop1, and unwinds
after wake.

The Stop1 entry path needs to:

- Confirm no bus transfer, flash write/erase, debug monitor transaction, or
  active log operation requires run-mode clocks.
- Park SPI, I2C, USART, and GPIO pins using the existing bus/device sleep
  policy helpers.
- Configure the wake sources before setting `SLEEPDEEP`.
- Select Stop1 in `PWR->CR1`.
- Execute `__WFI()`.
- Clear `SLEEPDEEP` after wake.
- Re-run `stm32_clock_init()` if Stop1 exit changes SYSCLK or peripheral
  kernel clocks.
- Restore bus/device pin modes only when the owning driver opens a bus session.

Avoid entering Stop1 while holding a ChibiOS system lock longer than required
for the final sleep transition. The post-wake clock restore and device unwind
must run unlocked unless a specific ChibiOS API requires otherwise.

### 3. Decide How ChibiOS Time Advances in Stop1

There are two viable approaches.

Option A is the smallest first milestone: use Stop1 only for indefinite waits
on the LSM EXTI wake source. Do not expect `chThdSleep*()` or
`chBSemWaitTimeout()` deadlines to expire while the core is in Stop1. This is
acceptable for a sensor-paced collection loop if all periodic work is tied to
external sensor interrupts or RTC wakeups.

Option B is the robust solution: provide a ChibiOS ST low-level driver backed
by an LPTIM clocked from LSI. This makes kernel timeouts continue across Stop1
without being limited by the board's 1024 Hz LSE source. Use LPTIM3 or LPTIM4
through the U3 `STM32_LPTIM34SEL` clock selector so the RTC/LSE path can remain
independent.

For an LSI-backed 16-bit LPTIM ST driver:

```c
#define HAL_ST_USE_TIMER_WIDTH              16
#define ST_LLD_NUM_ALARMS                   1
#define ST_LLD_HAS_PERIODIC_IRQ             FALSE

#define CH_CFG_ST_RESOLUTION                16
#define CH_CFG_ST_FREQUENCY                 8000
#define CH_CFG_ST_TIMEDELTA                 2
```

The 8000 Hz frequency is the intended ChibiOS ST tick rate. The LSI source is
nominally around 32 kHz, but it is not exact; choose the LPTIM prescaler and
calibration policy so `CH_CFG_ST_FREQUENCY` matches the effective timer rate
used by the driver. A 16-bit timer at 8000 Hz gives an 8.192 second hardware
wrap interval. Use a different frequency only after checking ChibiOS tickless
limits, timeout range, and LPTIM compare latency.

### 4. Implement the LPTIM ST Driver Carefully

The custom ST LLD should live in project source, not in `ChibiOS/`, unless the
submodule is intentionally being changed.

```c
#define HAL_ST_USE_TIMER_WIDTH             16
#define ST_LLD_NUM_ALARMS                  1
#define ST_LLD_HAS_PERIODIC_IRQ            FALSE

#ifdef __cplusplus
extern "C" {
#endif
  void st_lld_init(void);
  bool st_lld_is_alarm_active(void);
  systime_t st_lld_get_counter(void);
  void st_lld_start_alarm(systime_t time);
  void st_lld_stop_alarm(void);
#ifdef __cplusplus
}
#endif
```

Implementation checkpoints:

- Use the STM32U3 clock-source symbol shape. For LPTIM3/4 on this target that
  is `STM32_LPTIM34SEL`, not the STM32L-style `STM32_LPTIM3SEL`.
- Enable LSI and route `STM32_LPTIM34SEL` to the U3 LSI selector for the
  selected system-timer LPTIM. Keep any RTC-specific LSE users separate.
- Enable the selected LPTIM peripheral clock and reset it through ChibiOS/RCC
  helpers if the port provides them.
- If Stop1 wake requires autonomous peripheral clock requests on STM32U3,
  verify the exact RCC register and bit name before using it. Do not assume
  `RCC->SRDAMR |= RCC_SRDAMR_LPTIM3AMEN` without checking the U375 header.
- Configure ARR after enabling the LPTIM, then wait for `ARROK` after the
  write.
- Write CMP, then wait for `CMPOK` after the write.
- Clear stale compare flags before enabling `CMPMIE`.
- Handle compare values that are too close to the current counter; schedule at
  least `CH_CFG_ST_TIMEDELTA` ticks in the future.
- Enable the matching NVIC vector with `STM32_ST_IRQ_PRIORITY`.
- In the ISR, clear the LPTIM compare flag and call `chSysTimerHandlerI()`
  while locked from ISR.

Skeleton:

```c
void st_lld_init(void)
{
  /* Enable/reset selected LPTIM clock here. */

  LPTIM3->IER = 0;
  LPTIM3->ICR = 0xFFFFFFFFU;
  LPTIM3->CFGR = LPTIM_CFGR_PRESC_1; /* LSI-derived, target 8000 Hz ST. */
  LPTIM3->CR = LPTIM_CR_ENABLE;

  LPTIM3->ARR = 0xFFFFU;
  while ((LPTIM3->ISR & LPTIM_ISR_ARROK) == 0U) {
  }
  LPTIM3->ICR = LPTIM_ICR_ARROKCF;

  nvicEnableVector(STM32_LPTIM3_NUMBER, STM32_ST_IRQ_PRIORITY);
  LPTIM3->CR |= LPTIM_CR_CNTSTRT;
}

bool st_lld_is_alarm_active(void)
{
  return (LPTIM3->IER & LPTIM_IER_CMPMIE) != 0U;
}

systime_t st_lld_get_counter(void)
{
  return (systime_t)LPTIM3->CNT;
}

void st_lld_start_alarm(systime_t time)
{
  LPTIM3->IER &= ~LPTIM_IER_CMPMIE;
  LPTIM3->ICR = LPTIM_ICR_CMPMCF;
  LPTIM3->CMP = (uint32_t)time;
  while ((LPTIM3->ISR & LPTIM_ISR_CMPOK) == 0U) {
  }
  LPTIM3->ICR = LPTIM_ICR_CMPOKCF;
  LPTIM3->IER |= LPTIM_IER_CMPMIE;
}

void st_lld_stop_alarm(void)
{
  LPTIM3->IER &= ~LPTIM_IER_CMPMIE;
}

OSAL_IRQ_HANDLER(STM32_LPTIM3_HANDLER)
{
  OSAL_IRQ_PROLOGUE();
  if ((LPTIM3->ISR & LPTIM_ISR_CMPM) != 0U) {
    LPTIM3->ICR = LPTIM_ICR_CMPMCF;
    chSysLockFromISR();
    chSysTimerHandlerI();
    chSysUnlockFromISR();
  }
  OSAL_IRQ_EPILOGUE();
}
```

The EXTI wake line for LPTIM3 must be verified before coding. If the wake line
is above 31, it will not belong in `EXTI->IMR1`; use the register and bit from
the STM32U375 header/reference manual.

## Peripheral State in Stop1

Do not keep SPI, I2C, or GPDMA active through Stop1 for the first milestone.
The safer policy is:

- Finish any active transfer before idle can enter Stop1.
- Deselect SPI devices and park bus pins.
- Keep sensor power rails enabled only for devices that must keep sampling.
- Keep the LSM interrupt pin configured as an EXTI wake source.
- Reopen buses normally after wake through existing driver begin/end helpers.

Only consider autonomous SPI/I2C/DMA operation after the simple Stop1 wake path
is stable and measured.

## Debugger Behavior

When a debugger is attached, either skip Stop1 or enable the STM32 debug-in-stop
bits intentionally. Guard any debug behavior with `CoreDebug->DHCSR` so release
firmware does not keep debug support powered accidentally.

The debug path should be explicit because keeping debug active in Stop mode can
hide wake/clock bugs and changes current consumption.

## Clock Cautions

Keep the U375 SYSCLK at or below the current 24 MHz MSIS plan until Stop1 wake
and clock restore are measured. The target currently uses `STM32_STOPWUCK` for
MSIS wake and `STM32_STOPKERWUCK` for MSIK kernel clocks; changing those should
be treated as a separate clock-tree experiment.

After every Stop1 wake, verify:

- `SystemCoreClock`
- SPI1 kernel clock
- I2C kernel clock
- ChibiOS timebase
- RTC/LSE state
- LPTIM34/LSI state
- LSM FIFO interrupt behavior

## SRAM1 Power-Down

SRAM1 power-down is a separate, later optimization. Do not combine it with the
first Stop1 implementation.

Before setting `PWR_CR1_SRAM1PD` in run mode, prove from the linker map that no
live section is in SRAM1:

- vector table, if relocated
- main stack and process stacks
- ChibiOS thread working areas
- heap/core allocator pools
- `.data`, `.bss`, `.noinit`, and retained diagnostic buffers
- DMA buffers

If all live RAM moves to SRAM2, then a linker-memory change similar to this may
be appropriate after verifying the STM32U375 memory map:

```ld
ram0 (xrw) : ORIGIN = 0x20030000, LENGTH = 64K
ram1 (xrw) : ORIGIN = 0x00000000, LENGTH = 0
```

Stop-mode SRAM page retention uses different bits from run-mode SRAM1
power-down. Treat `PWR_CR1_SRAM1PD` and the `PWR_CR2_SRAM1PDSx` page controls
as separate features.

## Validation Checklist

1. Build the U375 target with no low-power changes and record baseline current.
2. Confirm the LSM EXTI callback wakes a blocked thread in normal sleep.
3. Enter Stop1 only on an indefinite LSM wait; verify wake and FIFO drain.
4. Verify clocks after wake with SPI flash, I2C sensors, and USART/monitor.
5. Measure current with sensor rails on and buses parked.
6. Add the LPTIM ST driver and verify `chThdSleepMilliseconds()` advances
   correctly across Stop1.
7. Verify `chBSemWaitTimeout()` wakes from timeout with no sensor interrupt.
8. Run long collection at each supported IMU ODR and check sample gaps.
9. Only after Stop1 is stable, evaluate SRAM1 power-down.
