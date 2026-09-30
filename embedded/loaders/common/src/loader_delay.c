/**
 * @file    loader_delay.c
 * @brief   Busy-wait delays for external loaders, timed by the DWT cycle
 *          counter.
 *
 * @details The loader runs with interrupts disabled and no system tick, so
 *          the OSAL's sleeps -- which wait on a SysTick-driven virtual timer
 *          -- would never return. Counting core cycles needs neither.
 */

#include "loader.h"

/** @brief Core cycles per microsecond at the loader's fixed SYSCLK. */
#define LOADER_CYCLES_PER_US (STM32_SYSCLK / 1000000U)

/** @brief Longest single wait, keeping cycle arithmetic far from wrap. */
#define LOADER_DELAY_CHUNK_US 1000000U

/* Contract documented in loader.h. */
void loaderDelayInit(void)
{
  CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
  DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}

/* Contract documented in loader.h. */
void loaderDelayUs(uint32_t us)
{
  while (us > 0U) {
    const uint32_t chunk = us > LOADER_DELAY_CHUNK_US ? LOADER_DELAY_CHUNK_US : us;
    const uint32_t cycles = chunk * LOADER_CYCLES_PER_US;
    const uint32_t start = DWT->CYCCNT;

    while ((uint32_t)(DWT->CYCCNT - start) < cycles) {
    }
    us -= chunk;
  }
}

/* Contract documented in loader.h. */
void loaderDelayMs(uint32_t ms)
{
  while (ms > 0U) {
    loaderDelayUs(1000U);
    ms--;
  }
}
