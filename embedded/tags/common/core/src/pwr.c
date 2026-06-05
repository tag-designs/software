/**
 * @file pwr.c
 * @brief Common RTC bus lifecycle and MCU standby entry sequence.
 * @author tag firmware authors
 * @date 2026-05-23
 */

#include "hal.h"

#include "app.h"
#include "custom.h"
#include "device.h"
#include "persistent.h"
#include "power.h"

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

/** @name Common tag power sequence
 * Common tag power/standby sequence.
 *
 * The RTC remains here because every active tag uses the same RTC lifecycle.
 * Peripheral bindings such as external flash, sensors, and tag-specific buses
 * live in tag or family devices.c files, where descriptors and standby pin
 * policy are easier to audit.
 * @{
 */

/**
 * @brief Software-I2C delay callback used by the RTC I2C configuration.
 */
static void delay(void)
{
  __NOP();
}

static const I2CConfig rtc_i2c_config = {
    .delay = delay,
#if defined(SWAP_I2C) && SWAP_I2C
    .sda = LINE_RTC_SCL,
    .scl = LINE_RTC_SDA,
#else
    .sda = LINE_RTC_SDA,
    .scl = LINE_RTC_SCL,
#endif
};

static I2CDriver I2CRTC;
static binary_semaphore_t I2CRTCmutex;

const TagI2cController tagRtcI2cController = {
    .driver = &I2CRTC,
    .mutex = &I2CRTCmutex,
};

static const TagI2cDevice rtc_bus = {
    .controller = &tagRtcI2cController,
    .config = &rtc_i2c_config,
    .sda = LINE_RTC_SDA,
    .scl = LINE_RTC_SCL,
    .pwr = TAG_NO_LINE,
    .sleep_policy = TAG_I2C_SLEEP_PULLUP,
};

#if BOARD_STANDBY_HAS_CONFIG
static inline void tagApplyBoardStandbyPins(void)
{
#if HAS_PULLUPA
  PWR->PUCRA = PULLUPA;
#endif
#if HAS_PULLDWNA
  PWR->PDCRA = PULLDWNA;
#endif
#if HAS_PULLUPB
  PWR->PUCRB = PULLUPB;
#endif
#if HAS_PULLDWNB
  PWR->PDCRB = PULLDWNB;
#endif
#if HAS_PULLUPC
  PWR->PUCRC = PULLUPC;
#endif
#if HAS_PULLDWNC
  PWR->PDCRC = PULLDWNC;
#endif
}
#endif

/**
 * @brief Initialize power/RTC bus runtime state.
 */
void tagPowerInit(void)
{
  chBSemObjectInit(&I2CRTCmutex, false);
  tagI2cControllerObjectInit(rtc_bus.controller);
}

/**
 * @brief Power and begin the shared RTC bus session.
 */
void rtcOn(void)
{
  tagI2cDevicePowerOn(&rtc_bus);
  tagI2cBusBegin(&rtc_bus);
}

/**
 * @brief End the shared RTC bus session and remove device power.
 */
void rtcOff(void)
{
  tagI2cBusEnd(&rtc_bus);
  tagI2cDevicePowerOff(&rtc_bus);
}

/**
 * @brief Enter the requested low-power terminal mode after device preparation.
 *
 * @param[in] sleepmode Requested sleep mode.
 */
void godown(enum Sleep sleepmode)
{
  if (sleepmode == STOP1) {
    DBGMCU->CR = 0;
    MODIFY_REG(PWR->CR1, PWR_CR1_LPMS, PWR_CR1_LPMS_STOP1);
    SET_BIT(SCB->SCR, ((uint32_t)SCB_SCR_SLEEPDEEP_Msk));

#if STOP1_WAKE_EXTI_GROUP1_MASK
    extiClearGroup1(STOP1_WAKE_EXTI_GROUP1_MASK);
#endif
    __DSB();
    __SEV();
    __WFE();
    __WFE();

    CLEAR_BIT(SCB->SCR, ((uint32_t)SCB_SCR_SLEEPDEEP_Msk));
    return;
  }

  tagDevicesPrepareStandby(pState->state);

  __disable_irq();

  // disable the debug unit

  DBGMCU->CR = 0;

  // disable sram3

  CLEAR_BIT(PWR->CR3, PWR_CR3_RRS);

  // Pullup/Pulldown configuration

#if BOARD_STANDBY_HAS_CONFIG
  tagApplyBoardStandbyPins();
#else
  tagDevicesApplyStandbyPins();
  tagI2cDevicePrepareSleep(&rtc_bus);
#endif

  // Apply pull-up and pull-down configuration

  SET_BIT(PWR->CR3, PWR_CR3_APC);

  tagDevicesDisableWakeupSources();
  /*
   * TODO(CRITICAL): this abort path returns from godown() with interrupts
   * disabled. Rework standby entry so all early exits restore IRQ state.
   */
  if (!tagDevicesConfigureWakeupSources(pState->state, isActive))
    return;

  MODIFY_REG(PWR->CR1, PWR_CR1_LPMS, PWR_CR1_LPMS_STANDBY);
  SET_BIT(SCB->SCR, ((uint32_t)SCB_SCR_SLEEPDEEP_Msk));

  __DSB();
  __WFI();

  __enable_irq();
}

/**
 * @brief Convert an unexpected exception into a retained state marker and standby.
 */
void _unhandled_exception(void)
{
  pState->state = EXCEPTION;
  godown(STANDBY);
}
/** @} */
