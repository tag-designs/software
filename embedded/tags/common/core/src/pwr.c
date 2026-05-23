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

static const TagI2cDevice rtc_bus = {
    .controller = &tagI2c1DefaultController,
    .config = &rtc_i2c_config,
    .sda = LINE_RTC_SDA,
    .scl = LINE_RTC_SCL,
    .pwr = TAG_NO_LINE,
    .sleep_policy = TAG_I2C_SLEEP_PULLUP,
};

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
 * @param[in] sleepmode Requested sleep mode; currently all paths enter standby.
 */
void godown(enum Sleep sleepmode)
{
  (void)sleepmode;

  tagDevicesPrepareStandby(pState->state);

  __disable_irq();
  DBGMCU->CR = 0;

  CLEAR_BIT(PWR->CR3, PWR_CR3_RRS);

  tagDevicesApplyStandbyPins();
  tagI2cDevicePrepareSleep(&rtc_bus);

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
