/**
 * @file pwr.c
 * @brief Common RTC bus lifecycle and MCU standby entry sequence.
 * @author tag firmware authors
 * @date 2026-05-23
 */

#include "hal.h"

#include "app.h"
#include "core_types.h"
#include "custom.h"
#include "device.h"
#include "persistent.h"
#include "power.h"

#if defined(TAG_RTC_RV3028)
#include "rtc_device.h"
#endif

#ifndef BACKUP_STATE_VALID_MAGIC
#define BACKUP_STATE_VALID_MAGIC 1U
#endif

#if defined(__has_include)
#if __has_include("board_standby.h")
#include "board_standby.h"
#endif
#endif

#if !defined(BOARD_STANDBY_HAS_CONFIG)
#define BOARD_STANDBY_HAS_CONFIG 0
#endif

#if !defined(STOP1_WAKE_EXTI_GROUP1_MASK)
#define STOP1_WAKE_EXTI_GROUP1_MASK 0U
#endif

#ifndef TAG_HALT_ON_EXCEPTION_WHEN_MONCONNECTED
#define TAG_HALT_ON_EXCEPTION_WHEN_MONCONNECTED 0
#endif

#ifndef TAG_STOP1_WAKE_USES_INTERRUPT
#define TAG_STOP1_WAKE_USES_INTERRUPT 0
#endif

/** @name Common tag power sequence
 * Common tag power/standby sequence.
 *
 * The RTC remains here because every active tag uses the same RTC lifecycle.
 * Peripheral bindings such as external flash, sensors, and tag-specific buses
 * live in tag or family devices.c files, where descriptors and standby pin
 * policy are easier to audit.
 * @{
 */

static inline void tagPowerSelectStop1(void)
{
#if defined(PWR_CR1_LPMS_STOP1)
  MODIFY_REG(PWR->CR1, PWR_CR1_LPMS, PWR_CR1_LPMS_STOP1);
#elif defined(PWR_CR1_LPMS_0)
  MODIFY_REG(PWR->CR1, PWR_CR1_LPMS, PWR_CR1_LPMS_0);
#endif
}

static inline void tagPowerSelectStandby(void)
{
#if defined(PWR_CR1_LPMS_STANDBY)
  MODIFY_REG(PWR->CR1, PWR_CR1_LPMS, PWR_CR1_LPMS_STANDBY);
#elif defined(PWR_CR1_LPMS_0) && defined(PWR_CR1_LPMS_1)
  MODIFY_REG(PWR->CR1, PWR_CR1_LPMS, PWR_CR1_LPMS_0 | PWR_CR1_LPMS_1);
#endif
}

static inline void tagPowerDisableSramRetention(void)
{
#if defined(PWR_CR3_RRS)
  CLEAR_BIT(PWR->CR3, PWR_CR3_RRS);
#else
  uint32_t retention = 0U;
#if defined(PWR_CR1_RRSB1)
  retention |= PWR_CR1_RRSB1;
#endif
#if defined(PWR_CR1_RRSB2)
  retention |= PWR_CR1_RRSB2;
#endif
#if defined(PWR_CR1_RRSB3)
  retention |= PWR_CR1_RRSB3;
#endif
  CLEAR_BIT(PWR->CR1, retention);
#endif
}

static inline void tagPowerApplyStandbyPulls(void)
{
#if defined(PWR_CR3_APC)
  SET_BIT(PWR->CR3, PWR_CR3_APC);
#elif defined(PWR_APCR_APC)
  SET_BIT(PWR->APCR, PWR_APCR_APC);
#endif
}

static inline void tagPowerClearWakeFlags(void)
{
#if defined(PWR_SCR_CWUF)
  WRITE_REG(PWR->SCR, PWR_SCR_CWUF);
#elif defined(PWR_WUSCR_CWUF1)
  uint32_t clear = 0U;
#if defined(PWR_WUSCR_CWUF1)
  clear |= PWR_WUSCR_CWUF1;
#endif
#if defined(PWR_WUSCR_CWUF2)
  clear |= PWR_WUSCR_CWUF2;
#endif
#if defined(PWR_WUSCR_CWUF3)
  clear |= PWR_WUSCR_CWUF3;
#endif
#if defined(PWR_WUSCR_CWUF4)
  clear |= PWR_WUSCR_CWUF4;
#endif
#if defined(PWR_WUSCR_CWUF5)
  clear |= PWR_WUSCR_CWUF5;
#endif
#if defined(PWR_WUSCR_CWUF6)
  clear |= PWR_WUSCR_CWUF6;
#endif
#if defined(PWR_WUSCR_CWUF7)
  clear |= PWR_WUSCR_CWUF7;
#endif
#if defined(PWR_WUSCR_CWUF8)
  clear |= PWR_WUSCR_CWUF8;
#endif
#if defined(PWR_WUSCR_CWUF9)
  clear |= PWR_WUSCR_CWUF9;
#endif
#if defined(PWR_WUSCR_CWUF10)
  clear |= PWR_WUSCR_CWUF10;
#endif
  WRITE_REG(PWR->WUSCR, clear);
#endif
}

#if BOARD_STANDBY_HAS_CONFIG
void tagClearStandbyPulls(void)
{
  PWR->PUCRA = 0U;
  PWR->PDCRA = 0U;
  PWR->PUCRB = 0U;
  PWR->PDCRB = 0U;
  PWR->PUCRC = 0U;
  PWR->PDCRC = 0U;
}

static inline void tagApplyBoardStandbyPins(void)
{
  PWR->PUCRA = PULLUPA;
  PWR->PDCRA = PULLDWNA;
  PWR->PUCRB = PULLUPB;
  PWR->PDCRB = PULLDWNB;
  PWR->PUCRC = PULLUPC;
  PWR->PDCRC = PULLDWNC;
}
#else
void tagClearStandbyPulls(void)
{
}
#endif

/**
 * @brief Initialize power/RTC bus runtime state.
 */
void tagPowerInit(void)
{
#if defined(TAG_RTC_RV3028)
  tagRtcDeviceRuntimeInit();
#endif
}

/**
 * @brief Power and begin the shared RTC bus session.
 */
void rtcOn(void)
{
#if defined(TAG_RTC_RV3028)
  const TagRtcDevice *rtc = tagRtcDevice();
  const TagI2cDevice *registers = rtc ? rtc->registers : NULL;

  if (registers)
  {
    tagI2cDevicePowerOn(registers);
    tagI2cBusBegin(registers);
  }
#endif
}

/**
 * @brief End the shared RTC bus session and remove device power.
 */
void rtcOff(void)
{
#if defined(TAG_RTC_RV3028)
  const TagRtcDevice *rtc = tagRtcDevice();
  const TagI2cDevice *registers = rtc ? rtc->registers : NULL;

  if (registers)
  {
    tagI2cBusEnd(registers);
    tagI2cDevicePowerOff(registers);
  }
#endif
}

/**
 * @brief Enter the requested low-power terminal mode after device preparation.
 *
 * @param[in] sleepmode Requested sleep mode.
 */
void godown(enum Sleep sleepmode)
{
  if (MONCONNECTED || (sleepmode == SLEEP))
  {
    return;
  }

  if (sleepmode == STOP1) {
    DBGMCU->CR = 0;
    tagPowerSelectStop1();
    SET_BIT(SCB->SCR, ((uint32_t)SCB_SCR_SLEEPDEEP_Msk));

#if STOP1_WAKE_EXTI_GROUP1_MASK
    extiClearGroup1(STOP1_WAKE_EXTI_GROUP1_MASK);
#if defined(LINE_ACCEL_INT)
    if (palReadLine(LINE_ACCEL_INT)) {
      CLEAR_BIT(SCB->SCR, ((uint32_t)SCB_SCR_SLEEPDEEP_Msk));
      return;
    }
#endif
#endif
    __DSB();
#if TAG_STOP1_WAKE_USES_INTERRUPT
    __WFI();
#else
    __SEV();
    __WFE();
    __WFE();
#endif

    CLEAR_BIT(SCB->SCR, ((uint32_t)SCB_SCR_SLEEPDEEP_Msk));
    return;
  }

  tagDevicesApplyPowerState(TAG_DEVICE_POWER_STANDBY_ENTRY, pState->state);

  __disable_irq();

  // disable the debug unit

  DBGMCU->CR = 0;

  // disable standby SRAM retention

  tagPowerDisableSramRetention();

  // Pullup/Pulldown configuration

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

  // Apply pull-up and pull-down configuration

  tagPowerApplyStandbyPulls();

  tagDevicesDisableWakeupSources();
  tagPowerClearWakeFlags();
  if (!tagDevicesConfigureWakeupSources(pState->state, isActive)) {
    __enable_irq();
    return;
  }

  tagPowerSelectStandby();
  SET_BIT(SCB->SCR, ((uint32_t)SCB_SCR_SLEEPDEEP_Msk));

  __DSB();
  __WFI();

  __enable_irq();
}

void _unhandled_exception(void)
{
  if (pState->valid == BACKUP_STATE_VALID_MAGIC)
  {
    pState->resetCause = resetException;
    pState->state = EXCEPTION;
  }
#if TAG_HALT_ON_EXCEPTION_WHEN_MONCONNECTED
  /*
   * Fault bring-up aid: leave the target stopped for debugger inspection
   * instead of immediately resetting. Keep disabled in normal firmware so an
   * attached monitor does not change exception recovery behavior.
   */
  if (MONCONNECTED)
  {
    while (true)
    {
    }
  }
#endif
  NVIC_SystemReset();
  while (true)
  {
  }
}
/** @} */
