/**
 * @file pwr-u375.c
 * @brief STM32U375 Stop3 terminal power path included by pwr.c.
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
  if ((sleepmode != STANDBY) || monitorIsAttached())
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
static void tagPowerResetSpi1BeforeStandby(void)
{
  rccResetSPI1();

  __DSB();
  __ISB();
}

#if 0
void Force_SPI1_GPDMA1_Reset_LL(void)
{
    // =========================================================================
    // STEP 1: Safely Suspend & Disable GPDMA Channel 0
    // =========================================================================
    if (GPDMA1_Channel0->CCR & DMA_CCR_EN) {
        // Request channel suspension to finish/flush active bus transfers
        GPDMA1_Channel0->CCR |= DMA_CCR_SUSP;

        // Poll for suspension acknowledgement (SUSPF flag in CSR)
        uint32_t timeout = 10000;
        while (!(GPDMA1_Channel0->CSR & DMA_CSR_SUSPF) && --timeout);

        // Disable channel
        GPDMA1_Channel0->CCR &= ~DMA_CCR_EN;
    }

    // Clear ALL GPDMA Channel 0 flags (Writing 1s to Clear Flag Register - CFCR)
    GPDMA1_Channel0->CFCR = 0xFFFFFFFF;

    // =========================================================================
    // STEP 2: Disable SPI1 Peripheral
    // =========================================================================
    // Clear SPE (SPI Enable) in CR1
    SPI1->CR1 &= ~SPI_CR1_SPE;

    // Clear any pending status/error flags by writing to IFCR (Interrupt/Flag Clear Reg)
    SPI1->IFCR = 0xFFFFFFFF;

    // =========================================================================
    // STEP 3: Pulse Hardware Peripheral Resets via RCC
    // =========================================================================
    // SPI1 Reset (APB2 Peripheral Reset Register - APB2RSTR)
    RCC->APB2RSTR |= RCC_APB2RSTR_SPI1RST;
    __NOP(); __NOP(); __NOP();
    RCC->APB2RSTR &= ~RCC_APB2RSTR_SPI1RST;

    // GPDMA1 Reset (AHB1 Peripheral Reset Register - AHB1RSTR)
    RCC->AHB1RSTR1 |= RCC_AHB1RSTR1_GPDMA1RST;
    __NOP(); __NOP(); __NOP();
    RCC->AHB1RSTR1 &= ~RCC_AHB1RSTR1_GPDMA1RST;

    // =========================================================================
    // STEP 4: Disable & Clear NVIC IRQ Lines at the Core Level
    // =========================================================================
    // Disable IRQs in NVIC (ICER - Interrupt Clear-Enable Register)
    NVIC_DisableIRQ(SPI1_IRQn);
    NVIC_DisableIRQ(GPDMA1_Channel0_IRQn);

    // Clear Pending IRQs in NVIC (ICPR - Interrupt Clear-Pending Register)
    NVIC_ClearPendingIRQ(SPI1_IRQn);
    NVIC_ClearPendingIRQ(GPDMA1_Channel0_IRQn);

    // =========================================================================
    // STEP 5: Gate Peripheral Clocks in RCC
    // =========================================================================
    // Disable SPI1 clock (APB2ENR)
    RCC->APB2ENR &= ~RCC_APB2ENR_SPI1EN;

    // Disable GPDMA1 clock (AHB1ENR)
    RCC->AHB1ENR1 &= ~RCC_AHB1ENR1_GPDMA1EN;

    // Memory barriers to ensure peripheral write operations complete
    __DSB();
    __ISB();
}
#endif

static void tagPowerEnterStandby(enum Sleep sleepmode)
{

  if ((sleepmode != STANDBY) || isMonitorEnabled())
  {
    return;
  }

  tagDevicesApplyPowerState(TAG_DEVICE_POWER_STANDBY_ENTRY, pState->state);

  if (!tagDevicesConfigureWakeupSources(pState->state, isActive))
  {
    return;
  }
  tagPowerConfigureStop3RtcWake();

  tagPowerResetSpi1BeforeStandby();

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
 * @brief Enter STM32U3 terminal Stop3 after device preparation.
 *
 * @param[in] sleepmode Requested sleep mode.
 */

static void tagPowerEnterTerminalSleep(enum Sleep sleepmode)
{
  tagPowerEnterStandby(sleepmode);
}
