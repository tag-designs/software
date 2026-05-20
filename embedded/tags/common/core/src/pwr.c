#include "hal.h"

#include "app.h"
#include "custom.h"
#include "device.h"
#include "persistent.h"
#include "power.h"

#ifndef ACCEL_WAKEUP_SOURCE
#define ACCEL_WAKEUP_SOURCE 4
#endif

#if ACCEL_WAKEUP_SOURCE == 1
#define ACCEL_WAKEUP_POLARITY_BIT PWR_CR4_WP1
#define ACCEL_WAKEUP_ENABLE_BIT PWR_CR3_EWUP1_Msk
#elif ACCEL_WAKEUP_SOURCE == 4
#define ACCEL_WAKEUP_POLARITY_BIT PWR_CR4_WP4
#define ACCEL_WAKEUP_ENABLE_BIT PWR_CR3_EWUP4_Msk
#else
#error "Unsupported ACCEL_WAKEUP_SOURCE"
#endif

/*
 * Common tag power/standby sequence.
 *
 * The RTC remains here because every active tag uses the same RTC lifecycle.
 * Peripheral bindings such as external flash, sensors, and tag-specific buses
 * live in tag or family devices.c files, where descriptors and standby pin
 * policy are easier to audit.
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

void rtcOn(void)
{
  tagI2cDevicePowerOn(&rtc_bus);
  tagI2cBusBegin(&rtc_bus);
}

void rtcOff(void)
{
  tagI2cBusEnd(&rtc_bus);
  tagI2cDevicePowerOff(&rtc_bus);
}

void godown(enum Sleep sleepmode)
{
  (void)sleepmode;

  tagDevicesPrepareStandby(pState->state);

  __disable_irq();
  DBGMCU->CR = 0;

  CLEAR_BIT(PWR->CR3, PWR_CR3_RRS);

#ifdef LINE_ACCEL_CS
  tagEnableStandbyPullup(LINE_ACCEL_CS);
#endif

  tagDevicesApplyStandbyPins();
  tagI2cDevicePrepareSleep(&rtc_bus);

  SET_BIT(PWR->CR3, PWR_CR3_APC);

  CLEAR_BIT(PWR->CR3, ACCEL_WAKEUP_ENABLE_BIT);

#if defined(LINE_ACCEL_INT)
  if (isActive)
  {
    SET_BIT(PWR->CR4, ACCEL_WAKEUP_POLARITY_BIT);
  }
  else
  {
    CLEAR_BIT(PWR->CR4, ACCEL_WAKEUP_POLARITY_BIT);
  }

  if (pState->state == RUNNING)
  {
    SET_BIT(PWR->CR3, ACCEL_WAKEUP_ENABLE_BIT | PWR_CR3_EIWF_Msk);
    if (isActive != palReadLine(LINE_ACCEL_INT))
      return;
  }
  else
  {
    SET_BIT(PWR->CR3, PWR_CR3_EIWF_Msk);
  }
#else
  SET_BIT(PWR->CR3, PWR_CR3_EIWF_Msk);
#endif

  MODIFY_REG(PWR->CR1, PWR_CR1_LPMS, PWR_CR1_LPMS_STANDBY);
  SET_BIT(SCB->SCR, ((uint32_t)SCB_SCR_SLEEPDEEP_Msk));

  __DSB();
  __WFI();

  __enable_irq();
}

void _unhandled_exception(void)
{
  pState->state = EXCEPTION;
  godown(STANDBY);
}
