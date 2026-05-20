#include "hal.h"
#include "app.h"
#include "device.h"
#include "persistent.h"
#include "power.h"

/*
 * Universal PresTag power/standby sequence.
 *
 * The RTC remains here because every active tag uses the same RTC lifecycle.
 * Non-universal peripherals such as pressure sensors and external flash live
 * in devices.c, where their board descriptors sit beside the tag/family hooks
 * used by the standby path.
 */

static void delay(void){
  __NOP();
}

static const I2CConfig rtc_i2c_config = {
    .delay = delay,
    .sda = LINE_RTC_SDA,
    .scl = LINE_RTC_SCL,
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

/*
 * Steps for entering standby
 *
 *   Set needed standby pullups
 *
 *   Disable all wakeup sources
 *   Clear all wakeup flags
 *   Re-enable wakeup sources
 *   Enter Standby
 */

void godown(enum Sleep sleepmode)
{
  (void) sleepmode;

  tagDevicesPrepareStandby(pState->state);

  // Make sure debug power is off
  __disable_irq();
  DBGMCU->CR = 0;
 
  // Mark the backup register.  Any reset at this point is ok
  // disable sram2 -- only works in standby, and pullup config

  CLEAR_BIT(PWR->CR3, PWR_CR3_RRS);             

  tagDevicesApplyStandbyPins();

  // Pull up SCL and SDA on RTC

  tagI2cDevicePrepareSleep(&rtc_bus);

  // turn on pullups

  SET_BIT(PWR->CR3, PWR_CR3_APC);

  // Enable internal wakeup source and set low power mode

  SET_BIT(PWR->CR3, PWR_CR3_EIWF_Msk);
  MODIFY_REG(PWR->CR1, PWR_CR1_LPMS, PWR_CR1_LPMS_STANDBY);

  // Set SLEEPDEEP bit of Cortex System Control Register

  SET_BIT(SCB->SCR, ((uint32_t)SCB_SCR_SLEEPDEEP_Msk));

  __DSB();
  __WFI();

 __enable_irq();
  // Should never return !;
}

// Non-maskable interrupt
// Go To Shutdown
void _unhandled_exception(void)
{
  pState->state = EXCEPTION;
  godown(STANDBY);
}
