/**
 * @file pwr.c
 * @brief Common RTC bus lifecycle and MCU standby entry sequence.
 * @author tag firmware authors
 * @date 2026-05-23
 */

#include "hal.h"

#include "app.h"
#include "core_events.h"
#include "core_sync.h"
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

#ifndef TAG_HALT_ON_EXCEPTION_WHEN_MONCONNECTED
#define TAG_HALT_ON_EXCEPTION_WHEN_MONCONNECTED 0
#endif

#if defined(STM32U3xx) || defined(STM32U3XX) || defined(STM32U375xx) || defined(STM32U385xx)

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
#ifndef TAG_STM32U3_TERMINAL_STOP3
/**
 * @brief Select Stop3 instead of Standby for terminal sleep on STM32U3 builds.
 */
#define TAG_STM32U3_TERMINAL_STOP3 1
#endif
#else
#ifndef TAG_STM32U3_TERMINAL_STOP3
/**
 * @brief Select Stop3 instead of Standby for terminal sleep on STM32U3 builds.
 */
#define TAG_STM32U3_TERMINAL_STOP3 0
#endif
#endif

#ifndef TAG_STM32U3_STOP3_CLEAR_WAKE_FLAGS
/**
 * @brief Clear STM32U3 PWR wake flags immediately before Stop3 entry.
 */
#define TAG_STM32U3_STOP3_CLEAR_WAKE_FLAGS 0
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

static inline void tagPowerSelectStandby(void)
{
#ifndef PWR_CR1_LPMS_STANDBY
#define PWR_CR1_LPMS_STANDBY 3
#endif
  MODIFY_REG(PWR->CR1, PWR_CR1_LPMS, PWR_CR1_LPMS_STANDBY);
}

#if TAG_STM32U3_TERMINAL_STOP3
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
 // MODIFY_REG(PWR->CR1,
 //           (4U << PWR_CR1_LPMS_Pos));
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
#if defined(HAL_LLD_USE_CLOCK_MANAGEMENT)
  if (hal_lld_clock_switch_mode(&hal_clkcfg_default)){
    palSetLine(LINE_LED1);
  }
#endif
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
  }MODIFY_REG(PWR->CR1, PWR_CR1_LPMS, 3U);

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

#endif

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

#if TAG_STM32U3_TERMINAL_STOP3
/**
 * @brief Enter STM32U3 terminal Stop3 after device preparation.
 *
 * @param[in] sleepmode Requested sleep mode.
 */
static void tagPowerEnterStop3(enum Sleep sleepmode)
{
  if ((sleepmode != STANDBY) || monitorIsAttached())
  {
    return;
  }

     palClearLine(LINE_LED1);
    chThdSleepMilliseconds(100);
    palSetLine(LINE_LED1);
    chThdSleepMilliseconds(100);

  tagDevicesApplyPowerState(TAG_DEVICE_POWER_STANDBY_ENTRY, pState->state);
  if (!tagDevicesConfigureWakeupSources(pState->state, isActive))
  {
    return;
  }
  tagPowerConfigureStop3RtcWake();
#if BOARD_STANDBY_HAS_CONFIG
  tagApplyBoardStandbyPins();
#else
  tagDevicesApplyStandbyPins();
#endif
  PWR->APCR |= PWR_APCR_APC;
  //chSysLock();

   RCC->AHB1ENR2 |= RCC_AHB1ENR2_PWREN;

  tagDevicesDisableWakeupSources();
  tagPowerClearWakeFlags();
  DBGMCU->CR = 0;
  MODIFY_REG(PWR->CR1, PWR_CR1_LPMS, 4U);

  //tagPowerSelectStop3();

  SET_BIT(SCB->SCR, ((uint32_t)SCB_SCR_SLEEPDEEP_Msk));

  __DSB();
  __WFI();
  __ISB();

  CLEAR_BIT(SCB->SCR, ((uint32_t)SCB_SCR_SLEEPDEEP_Msk));
  PWR->APCR &= ~PWR_APCR_APC;
  palSetLine(LINE_testpin);

  // this causes a transition to ABORTED --- there needs to be
  // communication with main to restart cleanly - probably emulate the
  // standby logic

  //pState->valid = BACKUP_STATE_VALID_MAGIC;
  //NVIC_SystemReset();

  chSysLock();
  tagPowerRestoreClocksAfterStop3();
  tagPowerRestoreFlashAfterStop3();
  tagPowerPostStop3WakeEventI();

  // 1. Request Range 1 (highest performance)
  //PWR->VOSR |= PWR_VOSR_RANGE1;


  chSysUnlock();
  // assert_flash_write_readiness();
   // Ensure Flash is awake and not stuck in low-power mode
  //FLASH->ACR &= ~(FLASH_ACR_SLEEP_PD|FLASH_ACR_LPM|FLASH_ACR_PDREQ1|FLASH_ACR_PDREQ2); 
  __DSB();

  
  while (1){
    palSetLine(LINE_LED1);
    chThdSleepMilliseconds(100);
    palClearLine(LINE_LED1);
    chThdSleepMilliseconds(100);
  }
  

}
#else
/**
 * @brief Enter STM32L4-style Standby after device preparation.
 *
 * @param[in] sleepmode Requested sleep mode.
 */
static void tagPowerEnterStandby(enum Sleep sleepmode)
{
  if ((sleepmode != STANDBY) || isMonitorEnabled())
  {
    return;
  }

  tagDevicesApplyPowerState(TAG_DEVICE_POWER_STANDBY_ENTRY, pState->state);

  chSysLock();

  DBGMCU->CR = 0;

  /* tagPowerDisableSramRetention(); */

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

  tagPowerApplyStandbyPulls();


  tagDevicesDisableWakeupSources();
  tagPowerClearWakeFlags();
  if (!tagDevicesConfigureWakeupSources(pState->state, isActive))
  {
    chSysUnlock();
    return;
  }

  tagPowerSelectStandby();

  PWR->CR3 |= PWR_CR3_APC;

  SET_BIT(SCB->SCR, ((uint32_t)SCB_SCR_SLEEPDEEP_Msk));

  __disable_irq();

  __DSB();
  __WFI();
  __ISB();

  CLEAR_BIT(SCB->SCR, ((uint32_t)SCB_SCR_SLEEPDEEP_Msk));
  chSysUnlock();
}
#endif

/**
 * @brief Enter the requested low-power terminal mode after device preparation.
 *
 * @param[in] sleepmode Requested sleep mode.
 */
void godown(enum Sleep sleepmode)
{
#if TAG_STM32U3_TERMINAL_STOP3
  tagPowerEnterStop3(sleepmode);
#else
  tagPowerEnterStandby(sleepmode);
#endif

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
