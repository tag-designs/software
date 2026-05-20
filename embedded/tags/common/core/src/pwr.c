#include "hal.h"
#include "custom.h"
#include "tag.pb.h"
#include "config.h"
#include "core_runtime.h"
#include "core_sync.h"
#include "device.h"
#include "gpio_utils.h"
#include "persistent.h"
#include "power.h"
#include "rtc_api.h"
#include "external_flash.h"
#include "lps.h"

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
 * I2C Devices
 */

static void delay(void){
  __NOP();
}

/*
 * I2C interface to the RTC.
 *
 * Some breakout/base-board revisions were fabricated with RTC_SDA and RTC_SCL
 * swapped. Define SWAP_I2C in the tag's custom.h for those boards so the
 * software I2C fallback drives the physical lines in the corrected order.
 */
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

// SPI Devices

#if defined(TAG_HAS_EXTERNAL_FLASH) && !defined(TAG_FLASH_AT25XE) && !defined(TAG_FLASH_MX25R)
static const TagSpiDevice flash_bus = {
    .controller = &tagSpi1DefaultController,
    .config = &tagSpiDefaultConfig,
    .cs = LINE_FLASH_nCS,
    .sck = LINE_FLASH_SCK,
    .miso = LINE_FLASH_MISO,
    .mosi = LINE_FLASH_MOSI,
    .pwr = TAG_NO_LINE,
    .dummy = 0xff,
    .sleep_policy = TAG_SPI_SLEEP_SAFE_IDLE,
};
#endif

#ifdef LPS_USART
static const TagUsartDevice lps_usart_device = {
    .controller = &tagUsart2SyncController,
    .config = &tagUsart2SyncDefaultConfig,
    .cs = LINE_LPS_CS,
    .sck = LINE_LPS_SCK,
    .tx = LINE_LPS_TX,
    .rx = LINE_LPS_RX,
    .pwr = LINE_LPS_PWR,
    .dummy = 0xff,
    .sleep_policy = TAG_USART_SLEEP_FLOAT,
};

void lpsPowerOn(void)
{
  tagUsartDevicePowerOn(&lps_usart_device);
}

void lpsPowerOff(void)
{
  tagUsartDevicePowerOff(&lps_usart_device);
}

void lpsBusBegin(void)
{
  tagUsartBusBegin(&lps_usart_device);
}

void lpsBusEnd(void)
{
  tagUsartBusEnd(&lps_usart_device);
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

#ifdef LPS_SPI
static const TagSpiDevice lps_bus = {
    .controller = &tagSpi1DefaultController,
    .config = &tagSpiDefaultConfig,
    .cs = LINE_STEVAL_CS,
    .sck = LINE_STEVAL_SCK,
    .miso = LINE_STEVAL_MISO,
    .mosi = LINE_STEVAL_MOSI,
    .pwr = LINE_STEVAL_PWR,
    .dummy = 0xff,
    .sleep_policy = TAG_SPI_SLEEP_FLOAT,
};

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

#if (defined(TAG_SENSOR_ACCEL_ADXL362) || defined(USE_ADXL367) || defined(USE_LIS2DU12)) && !defined(ACCEL_USART)
static const TagSpiDevice accel_bus = {
    .controller = &tagSpi1DefaultController,
    .config = &tagSpiDefaultConfig,
    .cs = LINE_ACCEL_CS,
    .sck = LINE_ACCEL_SCK,
    .miso = LINE_ACCEL_MISO,
    .mosi = LINE_ACCEL_MOSI,
    .pwr = TAG_NO_LINE,
    .dummy = 0xff,
    .sleep_policy = TAG_SPI_SLEEP_SAFE_IDLE,
};

void accelSpiOn()
{
  tagSpiBusBegin(&accel_bus);
}

void accelSpiOff()
{
  tagSpiBusEnd(&accel_bus);
}

void accelBusBegin(void)
{
  accelSpiOn();
}

void accelBusEnd(void)
{
  accelSpiOff();
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
 * Standby/Shutdown modes
 */

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

#if defined(TAG_HAS_EXTERNAL_FLASH) && !defined(TAG_FLASH_AT25XE) && !defined(TAG_FLASH_MX25R)
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

  // Pull up CS on ACCEL

#ifdef LINE_ACCEL_CS
  tagEnableStandbyPullup(LINE_ACCEL_CS);
#endif

  tagDevicesApplyStandbyPins();

#if defined(TAG_HAS_EXTERNAL_FLASH) && !defined(TAG_FLASH_AT25XE) && !defined(TAG_FLASH_MX25R)
  tagSpiDevicePrepareSleep(&flash_bus);
#endif

  tagPrepareDevicesForStandby();

  // Pull up SCL and SDA on RTC

  tagI2cDevicePrepareSleep(&rtc_bus);

  // turn on pullups

  SET_BIT(PWR->CR3, PWR_CR3_APC);

  // Disable wakeup source 4  why ?

  CLEAR_BIT(PWR->CR3, ACCEL_WAKEUP_ENABLE_BIT);

  #if defined(LINE_ACCEL_INT)
  if (isActive)
  {
    SET_BIT(PWR->CR4, ACCEL_WAKEUP_POLARITY_BIT); // falling edge detect
  }
  else
  {
    CLEAR_BIT(PWR->CR4, ACCEL_WAKEUP_POLARITY_BIT); // rising edge detect
  }

  // enable wakeup on adxl input only in running state

  if (pState->state == RUNNING)
  {
    SET_BIT(PWR->CR3, ACCEL_WAKEUP_ENABLE_BIT | PWR_CR3_EIWF_Msk);
    // if adxl input has changed since read, don't sleep
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

  // Enable internal wakeup source and set low power mode

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

void __attribute__((weak)) tagPrepareDevicesForStandby(void)
{
}
