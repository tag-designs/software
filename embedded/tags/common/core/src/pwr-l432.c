/**
 * @file pwr-l432.c
 * @brief STM32L432 Standby terminal power path included by pwr.c.
 * @author tag firmware authors
 * @date 2026-08-05
 *
 * @details This file is included from pwr.c so it can share common RTC,
 *          standby-pin, and wake-flag helpers without exporting additional
 *          symbols.
 */

#ifndef PWR_CR1_LPMS_STANDBY
#define PWR_CR1_LPMS_STANDBY 3
#endif

#ifndef PWR_CR1_LPMS_SHUTDOWN
#define PWR_CR1_LPMS_SHUTDOWN 4
#endif

/**
 * @brief Enter STM32L4 terminal sleep after device preparation.
 *
 * @param[in] sleepmode Requested sleep mode.
 */
static void tagPowerEnterTerminalSleep(enum Sleep sleepmode)
{
  uint32_t lpms;

  if (isMonitorEnabled())
  {
    return;
  }

  if (tagPowerTerminalModeEntersStandby(sleepmode))
  {
    lpms = PWR_CR1_LPMS_STANDBY;
  }
  else if (tagPowerTerminalModeEntersShutdown(sleepmode))
  {
    lpms = PWR_CR1_LPMS_SHUTDOWN;
  }
  else
  {
    return;
  }

  tagDevicesApplyPowerState(TAG_DEVICE_POWER_STANDBY_ENTRY, pState->state);

  chSysLock();

  DBGMCU->CR = 0;
#if defined(PWR_CR3_RRS)
  CLEAR_BIT(PWR->CR3, PWR_CR3_RRS);
#endif

#if BOARD_STANDBY_HAS_CONFIG
  tagApplyBoardStandbyPins();
#else
  tagDevicesApplyStandbyPins();
#if defined(TAG_RTC_RV3028)
  const TagRtcDevice *rtc = tagRtcDevice();
  if (rtc && rtc->registers)
  {
    tagI2cDevicePrepareSleep(rtc->registers);
  }
#endif
#endif

#if defined(PWR_CR3_APC)
  SET_BIT(PWR->CR3, PWR_CR3_APC);
#elif defined(PWR_APCR_APC)
  SET_BIT(PWR->APCR, PWR_APCR_APC);
#endif

  tagDevicesDisableWakeupSources();
  tagPowerClearWakeFlags();
  if (!tagDevicesConfigureWakeupSources(pState->state, isActive))
  {
    chSysUnlock();
    return;
  }

  tagPowerSetShutdownWakeMarker(lpms == PWR_CR1_LPMS_SHUTDOWN);
  MODIFY_REG(PWR->CR1, PWR_CR1_LPMS, lpms);

  PWR->CR3 |= PWR_CR3_APC;

  SET_BIT(SCB->SCR, ((uint32_t)SCB_SCR_SLEEPDEEP_Msk));

  __disable_irq();

  __DSB();
  __WFI();
  __ISB();

  CLEAR_BIT(SCB->SCR, ((uint32_t)SCB_SCR_SLEEPDEEP_Msk));
  chSysUnlock();
}
