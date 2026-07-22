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

#if (defined(STM32U375xx) || defined(STM32U385xx)) && defined(PWR_WUSCR_CWUF1)
/**
 *  Clear the U3 PWR wake flag after WKUP1 exits Stop1.
 */
OSAL_IRQ_HANDLER(Vector22C)
{
  OSAL_IRQ_PROLOGUE();

  WRITE_REG(PWR->WUSCR, PWR_WUSCR_CWUF1);

  OSAL_IRQ_EPILOGUE();
}
#endif

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

#if defined(STM32U375xx) || defined(STM32U385xx)
    chSysLock();

    rccEnablePWRInterface(true);

    /* Keep the PWR interface clock enabled in sleep while WKUP1 is armed. */
    /* WKUP1 must be disabled while its source mux and polarity are changed. */
    CLEAR_BIT(PWR->WUCR1, PWR_WUCR1_WUPEN1);

    /* PA0 is the board WKUP1 signal; WUSEL1=0 selects the WKUP1_0 route. */
    MODIFY_REG(PWR->WUCR3, PWR_WUCR3_WUSEL1, 0U);

    /* WUPP1=0 wakes on the active-high/rising WKUP1 assertion from the IMU. */
    CLEAR_BIT(PWR->WUCR2, PWR_WUCR2_WUPP1);

    /* Clear any sticky WKUP1 flag after selecting the source, then arm WKUP1. */
    WRITE_REG(PWR->WUSCR, PWR_WUSCR_CWUF1);
    SET_BIT(PWR->WUCR1, PWR_WUCR1_WUPEN1);

    /* Select Stop1 and make WFI enter deep sleep instead of normal sleep. */
    MODIFY_REG(PWR->CR1, PWR_CR1_LPMS, (0 << PWR_CR1_LPMS_Pos)); // switch 1U to 0

    /* PWR_IRQn must be enabled so WKUP1 can return from WFI in Stop1. */
    //NVIC_SetPriority(PWR_IRQn, 1);
    NVIC_ClearPendingIRQ(PWR_IRQn);
    //NVIC_DisableIRQ(PWR_IRQn);

    SET_BIT(SCB->SCR, ((uint32_t)SCB_SCR_SLEEPDEEP_Msk));

    PWR->CR1 &= ~(PWR_CR1_SRAM1PD | PWR_CR1_SRAM2PD);

    /* PB1 is high only while the U3 core is waiting for hardware WKUP1. */
    palSetPadMode(GPIOB, 1U, PAL_MODE_OUTPUT_PUSHPULL);
    palSetPad(GPIOB, 1U);

    /* Clear stale EXTI/event state before waiting for the next WKUP1 edge. */
#if STOP1_WAKE_EXTI_GROUP1_MASK
    extiClearGroup1(STOP1_WAKE_EXTI_GROUP1_MASK);
#endif

    /* If WKUP1 asserted while arming Stop1, keep running and service it now. */
    if (!palReadLine(LINE_WKUP1)) {
      __DSB();
      __ISB();
      __SEV();
      __WFE();
      __WFE();

      // Clear the power flag in hardware so you can drop back to sleep later
      PWR->WUSCR |= (1 << PWR_WUSCR_CWUF1_Pos);
      __DSB(); // Wait for the 14-cycle APB write delay to settle

    }

    palClearPad(GPIOB, 1U);
    CLEAR_BIT(PWR->WUCR1, PWR_WUCR1_WUPEN1);
    WRITE_REG(PWR->WUSCR, PWR_WUSCR_CWUF1);
    chSysUnlock();
    return;
    //tagDevicesAfterStop1(pState->state);
#else
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
