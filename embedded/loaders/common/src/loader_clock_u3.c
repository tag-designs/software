/**
 * @file    loader_clock_u3.c
 * @brief   STM32U375 loader clock: MSIS pinned at MSIRC1 / 2 = 12 MHz, by hand.
 *
 * @details The loader is entered at whatever clock the part reset to. On the
 *          bench IMUTagNandBmp581 the reset registers read MSIS = MSIRC1 / 2
 *          (12 MHz: RCC_ICSCR1 MSISSEL = 1, MSISDIV = 01, with RCC_CSR
 *          MSISDIVS = 01 in force while MSIRGSEL = 0). ChibiOS assumes MSIRC1 / 4
 *          (6 MHz) after reset instead. The DWT delays need the true value, so
 *          rather than trust either, this selects MSIRC1 / 2 explicitly through
 *          RCC_ICSCR1 (MSIRGSEL = 1). It is a value the part may already be
 *          running at, and it stays within voltage range 2 at the reset flash
 *          latency (1 WS; 0 WS suffices up to 16 MHz).
 *
 *          Touched: RCC_ICSCR1 (MSIS source and divider; the MSIK fields and
 *          trims are kept as found), and RCC_CFGR1/CFGR2 only to check that
 *          SYSCLK is MSIS with no AHB prescaler. Not touched: PWR (voltage
 *          range, booster), FLASH_ACR, RCC_BDCR or anything in the backup
 *          domain. ChibiOS's stm32_clock_init() is never called: on the U3 it
 *          resets every peripheral, sets DBP and calls bd_reset().
 *
 * @see     embedded/loaders/design/u375-nand-loader-plan.md, decision 3
 */

#include "loader.h"

#if STM32_SYSCLK != 12000000U
#error "loader_clock_u3.c pins MSIS at 12 MHz; mcuconf.h must describe SYSCLK = 12 MHz"
#endif

/** @brief Upper bound on register polls while the clock settles. */
#define LOADER_CLOCK_POLL_LIMIT 200000U

/**
 * @brief   Poll @p reg until (*reg & mask) == value, within a bound.
 * @return  false when LOADER_CLOCK_POLL_LIMIT polls were not enough.
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
  uint32_t icscr1;

  /* SYSCLK must already be MSIS (SWS = 00, the reset source) with
     HCLK = SYSCLK. */
  if ((RCC->CFGR1 & RCC_CFGR1_SWS_Msk) != 0U)
    return false;
  if ((RCC->CFGR2 & RCC_CFGR2_HPRE_Msk) != 0U)
    return false;

  icscr1 = RCC->ICSCR1;
  icscr1 &= ~(RCC_ICSCR1_MSISSEL_Msk | RCC_ICSCR1_MSISDIV_Msk);
  icscr1 |= RCC_ICSCR1_MSISSEL | RCC_ICSCR1_MSISDIV_0 | RCC_ICSCR1_MSIRGSEL;
  RCC->ICSCR1 = icscr1;

  return loaderClockWait(&RCC->CR, RCC_CR_MSISRDY, RCC_CR_MSISRDY);
}
