/**
 * @file pwr-u375.c
 * @brief STM32U375 returned-idle and terminal power paths included by pwr.c.
 * @author tag firmware authors
 * @date 2026-08-05
 *
 * @details This file is included from pwr.c so it can use the shared static
 *          helpers and compile-time board policy without exporting additional
 *          symbols.
 */

#if 0
void assert_flash_write_readiness(void) {
    bool incorrect_state = false;

    // 1. CHECK VOLTAGE RANGE (VCORE Scaling)
    // On STM32U3, VOSSR (Voltage Output Scale Status Register) tracks current core voltage.
    // If it reads Range 2 (low voltage), FLASH writes are prohibited by hardware and will trip PGSERR.
    #if defined(PWR_VOSSR_VOS)
    if ((PWR->VOSSR & PWR_VOSSR_VOS) != PWR_VOSSR_VOS_RANGE1) { 
        incorrect_state = true;
    }
    #else
    // Fallback check matching the ST Microelectronics RM0456 Reference Manual:
    // Ensure VOS bitfield is in High Performance Mode (Range 1)
    if ((PWR->VOSR & (PWR_VOSR_R2RDY_Msk|PWR_VOSR_R1RDY_Msk)) == 0) { // If 0 denotes Range 2 on this register variant
        incorrect_state = true;
    }
    #endif

    // 2. CHECK FLASH ERROR FLAGS 
    // If PGSERR (Programming Sequence Error) or WRPERR (Write Protection Error)
    // are already stuck high in the Flash Status Register, any new write command immediately aborts.
    if ((FLASH->SR & (FLASH_SR_PGSERR | FLASH_SR_WRPERR)) != 0) {
        incorrect_state = true;
    }

    // 3. FLAG THE INCORRECT STATE
    if (incorrect_state) {
        // Force the pin HIGH immediately via the ChibiOS PAL driver
        palSetLine(LINE_LED1);
        
        // OPTIONAL: Infinite loop here if you want to freeze the debugger at the point of failure
        // __asm__("bkpt #0"); 
    } else {
        // Ensure the pin is LOW if everything is safe for flash writing
        palClearLine(LINE_LED1);
    }
}
#endif

#ifndef TAG_STM32U3_STOP3_CLEAR_WAKE_FLAGS
/**
 * @brief Clear STM32U3 PWR wake flags immediately before Stop3 entry.
 */
#define TAG_STM32U3_STOP3_CLEAR_WAKE_FLAGS 0
#endif

#ifndef TAG_IDLE_STOP_DIAGNOSTICS
/**
 * @brief Enable optional GPIO diagnostics around returned STOP/WFI entry.
 */
#define TAG_IDLE_STOP_DIAGNOSTICS 0
#endif

/**
 * @brief Convert a returned idle sleep selector into the STM32U3 LPMS field.
 *
 * @param[in] mode Requested returned idle sleep mode.
 * @return PWR_CR1_LPMS field value for @c STOP0, @c STOP1, or @c STOP2.
 */
static inline uint32_t tagPowerIdleLpms(enum Sleep mode)
{
  switch (mode) {
  case STOP1:
#if defined(PWR_CR1_LPMS_STOP1)
    return PWR_CR1_LPMS_STOP1;
#elif defined(PWR_CR1_LPMS_0)
    return PWR_CR1_LPMS_0;
#else
    return 0U;
#endif
  case STOP2:
#if defined(PWR_CR1_LPMS_STOP2)
    return PWR_CR1_LPMS_STOP2;
#elif defined(PWR_CR1_LPMS_1)
    return PWR_CR1_LPMS_1;
#else
    return 0U;
#endif
  case STOP0:
  default:
    return 0U;
  }
}

/**
 * @brief Report whether a returned idle selector uses STM32U3 STOP mode.
 *
 * @param[in] mode Requested returned idle sleep mode.
 * @return true for @c STOP0, @c STOP1, and @c STOP2.
 */
static inline bool tagPowerIdleUsesStop(enum Sleep mode)
{
  return (mode == STOP0) || (mode == STOP1) || (mode == STOP2);
}

/**
 * @brief Normalize modes that must not use the returned-idle entry path.
 *
 * @param[in] mode Requested idle sleep depth.
 * @return A mode safe for in-place idle entry and wake return.
 */
static inline enum Sleep tagPowerReturnedIdleMode(enum Sleep mode)
{
  switch (mode) {
  case STOP0:
  case STOP1:
  case STOP2:
  case SLEEP:
    return mode;
  case STANDBY:
  case SHUTDOWN:
  default:
    return SLEEP;
  }
}

/**
 * @brief Enter a returned STM32U3 idle mode.
 *
 * @param[in] mode Requested idle sleep depth. @c STANDBY and @c SHUTDOWN are
 *                 treated as ordinary @c SLEEP because terminal modes remain
 *                 owned by godown().
 */
void tagPowerEnterIdleMode(enum Sleep mode)
{
  if (isMonitorEnabled()) {
    CLEAR_BIT(SCB->SCR, ((uint32_t)SCB_SCR_SLEEPDEEP_Msk));
    return;
  }

  mode = tagPowerReturnedIdleMode(mode);

  if (!tagPowerIdleUsesStop(mode)) {
    CLEAR_BIT(SCB->SCR, ((uint32_t)SCB_SCR_SLEEPDEEP_Msk));
    __DSB();
    __WFI();
    __DSB();
    __ISB();
    return;
  }

#if TAG_IDLE_STOP_DIAGNOSTICS
  palSetLine(LINE_LED1);
#endif

  DBGMCU->CR = 0;
  MODIFY_REG(PWR->CR1, PWR_CR1_LPMS, tagPowerIdleLpms(mode));
  SET_BIT(SCB->SCR, ((uint32_t)SCB_SCR_SLEEPDEEP_Msk));

  __DSB();
  __ISB();
  __WFI();
  __DSB();
  __ISB();

  CLEAR_BIT(SCB->SCR, ((uint32_t)SCB_SCR_SLEEPDEEP_Msk));

#if TAG_IDLE_STOP_DIAGNOSTICS
  palClearLine(LINE_LED1);
#endif
}

/**
 * @brief Select STM32U3 Stop3 as the next deep-sleep mode.
 */
static inline void tagPowerSelectStop3(void)
{
#if defined(PWR_CR1_LPMS_STOP3)
  MODIFY_REG(PWR->CR1, PWR_CR1_LPMS, PWR_CR1_LPMS_STOP3);
#elif defined(PWR_CR1_LPMS_0) && defined(PWR_CR1_LPMS_1)
  MODIFY_REG(PWR->CR1, PWR_CR1_LPMS, PWR_CR1_LPMS_0 | PWR_CR1_LPMS_1);
#else
  MODIFY_REG(PWR->CR1, PWR_CR1_LPMS, 3U);
#endif
}

#ifndef TAG_STM32U3_STOP3_RTC_WUCR1_ENABLE
#if defined(PWR_WUCR1_WUPEN7)
/**
 * @brief Wakeup-line enable bit used for RTC-originated Stop3 wake.
 */
#define TAG_STM32U3_STOP3_RTC_WUCR1_ENABLE PWR_WUCR1_WUPEN7
#else
#define TAG_STM32U3_STOP3_RTC_WUCR1_ENABLE 0U
#endif
#endif

#ifndef TAG_STM32U3_STOP3_RTC_WUCR2_POLARITY_MASK
#if defined(PWR_WUCR2_WUPP7)
/**
 * @brief Polarity mask for the RTC-originated Stop3 wake line.
 */
#define TAG_STM32U3_STOP3_RTC_WUCR2_POLARITY_MASK PWR_WUCR2_WUPP7
#else
#define TAG_STM32U3_STOP3_RTC_WUCR2_POLARITY_MASK 0U
#endif
#endif

#ifndef TAG_STM32U3_STOP3_RTC_WUCR2_POLARITY
/**
 * @brief Polarity value for the RTC-originated Stop3 wake line.
 */
#define TAG_STM32U3_STOP3_RTC_WUCR2_POLARITY 0U
#endif

#ifndef TAG_STM32U3_STOP3_RTC_WUCR3_SELECT_MASK
#if defined(PWR_WUCR3_WUSEL7)
/**
 * @brief Source-select mask for the RTC-originated Stop3 wake line.
 */
#define TAG_STM32U3_STOP3_RTC_WUCR3_SELECT_MASK PWR_WUCR3_WUSEL7
#else
#define TAG_STM32U3_STOP3_RTC_WUCR3_SELECT_MASK 0U
#endif
#endif

#ifndef TAG_STM32U3_STOP3_RTC_WUCR3_SELECT
#if defined(PWR_WUCR3_WUSEL7_0) && defined(PWR_WUCR3_WUSEL7_1)
/**
 * @brief Source-select value for the RTC-originated Stop3 wake line.
 */
#define TAG_STM32U3_STOP3_RTC_WUCR3_SELECT (PWR_WUCR3_WUSEL7_0 | PWR_WUCR3_WUSEL7_1)
#else
#define TAG_STM32U3_STOP3_RTC_WUCR3_SELECT TAG_STM32U3_STOP3_RTC_WUCR3_SELECT_MASK
#endif
#endif

/**
 * @brief Configure the STM32U3 PWR wake line used by RTC Stop3 wake.
 */
static inline void tagPowerConfigureStop3RtcWake(void)
{
#if TAG_STM32U3_STOP3_RTC_WUCR3_SELECT_MASK != 0U
  MODIFY_REG(PWR->WUCR3,
             TAG_STM32U3_STOP3_RTC_WUCR3_SELECT_MASK,
             TAG_STM32U3_STOP3_RTC_WUCR3_SELECT);
#endif
#if TAG_STM32U3_STOP3_RTC_WUCR2_POLARITY_MASK != 0U
  MODIFY_REG(PWR->WUCR2,
             TAG_STM32U3_STOP3_RTC_WUCR2_POLARITY_MASK,
             TAG_STM32U3_STOP3_RTC_WUCR2_POLARITY);
#endif
#if TAG_STM32U3_STOP3_RTC_WUCR1_ENABLE != 0U
  SET_BIT(PWR->WUCR1, TAG_STM32U3_STOP3_RTC_WUCR1_ENABLE);
#endif
}

/**
 * @brief Restore the configured STM32U3 run clock tree after Stop3 wake.
 */


static inline void tagPowerRestoreClocksAfterStop3(void)
{

#if 0 //defined(HAL_LLD_USE_CLOCK_MANAGEMENT)
  if (hal_lld_clock_switch_mode(&hal_clkcfg_default)){
    palSetLine(LINE_LED1);
  }
#endif

stm32_clock_init();
}

/**
 * @brief Return STM32U3 flash control/status to run-ready state after Stop3.
 */
static inline void tagPowerRestoreFlashAfterStop3(void)
{MODIFY_REG(PWR->CR1, PWR_CR1_LPMS, 3U);
  uint32_t acr_clear = 0U;
  uint32_t sr_clear = 0U;
  uint32_t sr_powerdown = 0U;

#if defined(FLASH_ACR_LPM)
  acr_clear |= FLASH_ACR_LPM;
#endif
#if defined(FLASH_ACR_PDREQ1)
  acr_clear |= FLASH_ACR_PDREQ1;
#endif
#if defined(FLASH_ACR_PDREQ2)
  acr_clear |= FLASH_ACR_PDREQ2;
#endif
#if defined(FLASH_ACR_SLEEP_PD)
  acr_clear |= FLASH_ACR_SLEEP_PD;
#endif
  if (acr_clear != 0U) {
    CLEAR_BIT(FLASH->ACR, acr_clear);
  }

#if defined(FLASH_SR_PD1)
  sr_powerdown |= FLASH_SR_PD1;
#endif
#if defined(FLASH_SR_PD2)
  sr_powerdown |= FLASH_SR_PD2;
#endif
  for (uint32_t timeout = 1024U;
       timeout > 0U && ((FLASH->SR & sr_powerdown) != 0U);
       timeout--) {
    __NOP();
  }

#if defined(FLASH_SR_EOP)
  sr_clear |= FLASH_SR_EOP;
#endif
#if defined(FLASH_SR_OPERR)
  sr_clear |= FLASH_SR_OPERR;
#endif
#if defined(FLASH_SR_PROGERR)
  sr_clear |= FLASH_SR_PROGERR;
#endif
#if defined(FLASH_SR_WRPERR)
  sr_clear |= FLASH_SR_WRPERR;
#endif
#if defined(FLASH_SR_PGAERR)
  sr_clear |= FLASH_SR_PGAERR;
#endif
#if defined(FLASH_SR_SIZERR)
  sr_clear |= FLASH_SR_SIZERR;
#endif
#if defined(FLASH_SR_PGSERR)
  sr_clear |= FLASH_SR_PGSERR;
#endif
#if defined(FLASH_SR_OPTWERR)
  sr_clear |= FLASH_SR_OPTWERR;
#endif
  if (sr_clear != 0U) {
    WRITE_REG(FLASH->SR, sr_clear);
  }
}

/**
 * @brief Post a synthetic terminal-wake event to the main thread.
 *
 * @pre The system lock is held by the caller.
 */
static inline void tagPowerPostStop3WakeEventI(void)
{
  if (tpMain != NULL)
  {
    chEvtSignalI(tpMain, EVT_WAKE_STANDBY);
  }
}

/**
 * @brief Latch a one-shot standby-wake marker and reset after Stop3.
 *
 * @details STM32U3 Stop3 wake does not assert the hardware SBF bit used by
 *          the legacy Standby resume path. This marker lets reset handling
 *          classify the intentional software reset as a standby wake without
 *          touching flash/cache state after Stop3 return.
 */
static void tagPowerResetAfterStop3Wake(void)
{
  pState->synthetic_standby_wake = TAG_SYNTHETIC_STANDBY_WAKE_MAGIC;
  pState->resetCause = resetStandby;

  __DSB();
  NVIC_SystemReset();
  while (true){}
}


static void __attribute__((unused)) tagPowerEnterStop3(enum Sleep sleepmode)
{
  if (!tagPowerTerminalModeEntersStandby(sleepmode) || monitorIsAttached())
  {
    return;
  }

  tagDevicesApplyPowerState(TAG_DEVICE_POWER_STANDBY_ENTRY, pState->state);
  if (!tagDevicesConfigureWakeupSources(pState->state, isActive))
  {
    return;
  }
  tagPowerConfigureStop3RtcWake();

#if TAG_STANDBY_PULLS_CONFIGURED_BY_MCUCONF
  /* Standby pulls were installed by ChibiOS from mcuconf.h at HAL startup. */
#elif BOARD_STANDBY_HAS_CONFIG
  tagApplyBoardStandbyPins();
#else
  tagDevicesApplyStandbyPins();
#endif
  PWR->APCR |= PWR_APCR_APC;

  RCC->AHB1ENR2 |= RCC_AHB1ENR2_PWREN;

  tagDevicesDisableWakeupSources();
  tagPowerClearWakeFlags();
  DBGMCU->CR = 0;
  MODIFY_REG(PWR->CR1, PWR_CR1_LPMS, 3U);

  //tagPowerSelectStop3();

  SET_BIT(SCB->SCR, ((uint32_t)SCB_SCR_SLEEPDEEP_Msk));

  __DSB();
  __ISB();
  __WFI();

  CLEAR_BIT(SCB->SCR, ((uint32_t)SCB_SCR_SLEEPDEEP_Msk));
  PWR->APCR &= ~PWR_APCR_APC;

  tagPowerResetAfterStop3Wake();
}


/**
 * @brief Reset SPI1 state before entering STM32U3 standby.
 *
 * @details IMUTagNand can remain in a high-current standby state after SPI1 has
 *          been used, even when the visible SPI and GPDMA registers look idle.
 *          A hardware reset pulse for SPI1 is sufficient to clear that
 *          condition; resetting GPDMA1 alone was not sufficient.
 *
 * @pre     Call only after all SPI-backed device standby preparation is done.
 */

 #if 0
static void tagPowerResetSpi1BeforeStandby(void)
{
  rccResetSPI1();

  __DSB();
  __ISB();
}
#endif

static void tagPowerEnterStandby(enum Sleep sleepmode)
{

  if (!tagPowerTerminalModeEntersStandby(sleepmode) || isMonitorEnabled())
  {
    return;
  }

  tagDevicesApplyPowerState(TAG_DEVICE_POWER_STANDBY_ENTRY, pState->state);

  if (!tagDevicesConfigureWakeupSources(pState->state, isActive))
  {
    return;
  }
  tagPowerConfigureStop3RtcWake();

  // not needed with proper spiStop()
  //tagPowerResetSpi1BeforeStandby();


#if TAG_STANDBY_PULLS_CONFIGURED_BY_MCUCONF
  /* Standby pulls were installed by ChibiOS from mcuconf.h at HAL startup. */
#elif BOARD_STANDBY_HAS_CONFIG
  tagApplyBoardStandbyPins();
#else
  tagDevicesApplyStandbyPins();
#endif
  PWR->APCR |= PWR_APCR_APC;

  RCC->AHB1ENR2 |= RCC_AHB1ENR2_PWREN;

  tagDevicesDisableWakeupSources();
  tagPowerClearWakeFlags();
  DBGMCU->CR = 0;
  MODIFY_REG(PWR->CR1, PWR_CR1_LPMS, 4U);


  SET_BIT(SCB->SCR, ((uint32_t)SCB_SCR_SLEEPDEEP_Msk));

  __DSB();
  __ISB();
  __WFI();

  // standby exits by resetting core

  //CLEAR_BIT(SCB->SCR, ((uint32_t)SCB_SCR_SLEEPDEEP_Msk));
  //PWR->APCR &= ~PWR_APCR_APC;

  //tagPowerResetAfterStop3Wake();
}

/**
 * @brief Enter STM32U3 terminal Stop3 after device preparation.
 *
 * @param[in] sleepmode Requested sleep mode.
 */

static void tagPowerEnterTerminalSleep(enum Sleep sleepmode)
{
  tagPowerEnterStandby(sleepmode);
}
