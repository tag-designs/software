/**
 * @file    loader_clock.c
 * @brief   SYSCLK setup for external loaders: HSI16, set by hand.
 *
 * @details ChibiOS's stm32_clock_init() is deliberately not used. It resets
 *          every peripheral, opens backup-domain write access, and can reset
 *          the whole backup domain (see cfg/stm32l4/mcuconf.h). This file
 *          writes only RCC_CR, RCC_CFGR and FLASH_ACR.
 *
 *          The loader may be entered with the part in any clock state the tag
 *          or the programmer left: MSI at 4 MHz after a reset, the tag's 2 MHz
 *          MSI in core-voltage range 2 on a hot attach, or something faster.
 *          HSI16 at 16 MHz is within range 2's 26 MHz limit, so the voltage
 *          range need not change.
 */

#include "loader.h"

#if STM32_SYSCLK != 16000000U
#error "loader_clock.c establishes HSI16; mcuconf.h must describe SYSCLK = 16 MHz"
#endif

/** @brief Iteration bound for oscillator and switch polls; ~tens of ms. */
#define LOADER_CLOCK_POLL_LIMIT 200000U

/**
 * @brief   Wait for (reg & mask) == value, bounded.
 *
 * @return  true when the condition held within LOADER_CLOCK_POLL_LIMIT reads.
 */
static bool loaderClockWait(volatile uint32_t *reg, uint32_t mask,
                            uint32_t value)
{
  uint32_t n;

  for (n = 0; n < LOADER_CLOCK_POLL_LIMIT; n++) {
    if ((*reg & mask) == value)
      return true;
  }
  return false;
}

/* Contract documented in loader.h. */
bool loaderClockInit(void)
{
  uint32_t acr;
  uint32_t cfgr;

  RCC->CR |= RCC_CR_HSION;
  if (!loaderClockWait(&RCC->CR, RCC_CR_HSIRDY, RCC_CR_HSIRDY))
    return false;

  /*
   * Two wait states cover 16 MHz in either voltage range. Latency is only
   * ever raised here, never lowered: if the part is currently running faster
   * than 16 MHz, its existing latency is already sufficient and lowering it
   * before the switch would violate it.
   */
  acr = FLASH->ACR;
  if ((acr & FLASH_ACR_LATENCY_Msk) < FLASH_ACR_LATENCY_2WS) {
    FLASH->ACR = (acr & ~FLASH_ACR_LATENCY_Msk) | FLASH_ACR_LATENCY_2WS;
    if (!loaderClockWait(&FLASH->ACR, FLASH_ACR_LATENCY_Msk,
                         FLASH_ACR_LATENCY_2WS))
      return false;
  }

  /* Switch source first, then prescalers, so HCLK never exceeds SYSCLK. */
  cfgr = RCC->CFGR;
  RCC->CFGR = (cfgr & ~RCC_CFGR_SW_Msk) | RCC_CFGR_SW_HSI;
  if (!loaderClockWait(&RCC->CFGR, RCC_CFGR_SWS_Msk, RCC_CFGR_SWS_HSI))
    return false;

  RCC->CFGR &= ~(RCC_CFGR_HPRE_Msk | RCC_CFGR_PPRE1_Msk | RCC_CFGR_PPRE2_Msk);
  return true;
}
