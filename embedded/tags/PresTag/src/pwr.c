#include "hal.h"
#include "app.h"
#include "external_flash.h"
#include "lps.h"
#include "persistent.h"
#include "power.h"
#if defined(TAG_FLASH_AT25XE) || defined(TAG_FLASH_MX25R)
#include "storage_flash.h"
#endif

/*
 * Device bus descriptors.
 *
 * PresTag keeps the existing public wrapper names (rtcOn/lpsOn/FlashSpiOn),
 * but the repetitive bus and sleep-pin mechanics now live in the shared
 * descriptor helpers from common/core.
 */

static void delay(void){
  __NOP();
}

static const TagI2cDevice rtc_bus = {
    .driver = &I2CD1,
    .mutex = &I2Cmutex,
    .config = {
        .delay = delay,
        .sda = LINE_RTC_SDA,
        .scl = LINE_RTC_SCL,
    },
    .sda = LINE_RTC_SDA,
    .scl = LINE_RTC_SCL,
    .pwr = TAG_NO_LINE,
};

#ifdef LPS_SPI
static const TagSpiDevice lps_bus = {
    .controller = &tagSpi1DefaultController,
    .config = &tagSpiDefaultConfig,
    .cs = LINE_STEVAL_CS,
    .sck = LINE_STEVAL_SCK,
    .miso = LINE_STEVAL_MISO,
    .mosi = LINE_STEVAL_MOSI,
    .pwr = LINE_STEVAL_PWR,
    .sleep_policy = TAG_SPI_SLEEP_FLOAT,
};
#endif

#if defined(TAG_HAS_EXTERNAL_FLASH) && !defined(TAG_FLASH_AT25XE) && !defined(TAG_FLASH_MX25R)
static const TagSpiDevice flash_bus = {
    .controller = &tagSpi1DefaultController,
    .config = &tagSpiDefaultConfig,
    .cs = LINE_FLASH_nCS,
    .sck = LINE_FLASH_SCK,
    .miso = LINE_FLASH_MISO,
    .mosi = LINE_FLASH_MOSI,
    .pwr = TAG_NO_LINE,
    .sleep_policy = TAG_SPI_SLEEP_SAFE_IDLE,
};
#endif

void rtcOn(void)
{
  tagI2cDeviceOn(&rtc_bus);
}

void rtcOff(void)
{
  tagI2cDeviceOff(&rtc_bus);
}

#ifdef LPS_SPI
void lpsPowerOn(void)
{
  tagSpiDevicePowerOn(&lps_bus);
}

void lpsPowerOff(void)
{
  tagSpiDevicePowerOff(&lps_bus);
}

void lpsBusBegin(void)
{
  tagSpiBusBegin(&lps_bus);
}

void lpsBusEnd(void)
{
  tagSpiBusEnd(&lps_bus);
}

void lpsOn(void)
{
  lpsPowerOn();
  lpsBusBegin();
}

void lpsOff(void)
{
  lpsBusEnd();
  lpsPowerOff();
}
#endif

#if defined(TAG_HAS_EXTERNAL_FLASH) && !defined(TAG_FLASH_AT25XE) && !defined(TAG_FLASH_MX25R)
void FlashSpiOn(void)
{
  tagSpiBusBegin(&flash_bus);
}

void FlashSpiOff(void)
{
  tagSpiBusEnd(&flash_bus);
}
#endif

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

#if defined(TAG_HAS_EXTERNAL_FLASH)
  // Make sure flash is in low power mode
  if ((pState->state == IDLE) ||
      (pState->state == ABORTED) ||
      (pState->state == FINISHED) ||
      (pState->state == EXCEPTION) ||
      (pState->state == HIBERNATING))
  {
    ExFlashPwrUp();
    stopMilliseconds(true,1);
    ExFlashPwrDown();
  }
#endif
  // Make sure debug power is off
  __disable_irq();
  DBGMCU->CR = 0;
 
  // Mark the backup register.  Any reset at this point is ok
  // disable sram2 -- only works in standby, and pullup config

  CLEAR_BIT(PWR->CR3, PWR_CR3_RRS);             

#if defined(TAG_FLASH_AT25XE) || defined(TAG_FLASH_MX25R)
  tagStoragePrepareSleep(&tagExternalFlash);
#elif defined(TAG_HAS_EXTERNAL_FLASH)
  tagSpiDevicePrepareSleep(&flash_bus);
#endif

  // Pull up SCL and SDA on RTC

  tagI2cDevicePrepareSleep(&rtc_bus);

  // fully discharge Pressure Sensor capacitor

  //enableLinePulldown(LINE_STEVAL_PWR);

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
