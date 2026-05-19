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
#ifdef TAG_SENSOR_MAG_AK09940A
#include "ak09940a.h"
#endif

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
const I2CConfig rtci2cConfig = {
    .delay = delay,
#if defined(SWAP_I2C) && SWAP_I2C
    .sda = LINE_RTC_SCL,
    .scl = LINE_RTC_SDA
#else
    .sda = LINE_RTC_SDA,
    .scl = LINE_RTC_SCL
#endif
};

void rtcOn(void)
{
  chBSemWait(&I2Cmutex); 
  palSetLine(LINE_RTC_SDA);
  palSetLine(LINE_RTC_SCL);
  toOutput(LINE_RTC_SCL);
  toOutput(LINE_RTC_SDA);
  i2cStart(&I2CD1, &rtci2cConfig);
}

void rtcOff(void)
{
  i2cStop(&I2CD1);
  chBSemSignal(&I2Cmutex); 
}

// SPI Devices

#if defined(LPS_SPI) || ((defined(TAG_SENSOR_ACCEL_ADXL362) || defined(USE_ADXL367) || defined(USE_LIS2DU12)) && !defined(ACCEL_USART))
static void spiEnable(void)
{
  rccEnableSPI1(0);
  rccResetSPI1();

  // Disable spi device

  SPI1->CR1 = 0;

  // Reset spi device

  // 1/4 fifo threshold, 8-bit data size

  SPI1->CR2 =
      SPI_CR2_FRXTH | SPI_CR2_SSOE | SPI_CR2_DS_2 
                     | SPI_CR2_DS_1 | SPI_CR2_DS_0;

  // master, enable spi device

  SPI1->CR1 = SPI_CR1_MSTR;
  SPI1->CR1 |= SPI_CR1_SPE;
  tagMarkSpi1On();
}

static void spiDisable(void)
{
  SPI1->CR1 = 0;
  SPI1->CR2 = 0;
  tagMarkSpi1Off();
}
#endif

#ifdef TAG_SENSOR_MAG_AK09940A
#if defined(LINE_MAG_CS) && defined(LINE_MAG_SCK) && defined(LINE_MAG_MISO) && defined(LINE_MAG_MOSI)
#define AK09940A_CS LINE_MAG_CS
#define AK09940A_SCK LINE_MAG_SCK
#define AK09940A_MISO LINE_MAG_MISO
#define AK09940A_MOSI LINE_MAG_MOSI
#else
#error "AK09940A power binding needs LINE_MAG_CS/SCK/MISO/MOSI aliases"
#endif

#if defined(LINE_MAG_PWR)
#define AK09940A_PWR LINE_MAG_PWR
#define AK09940A_SLEEP_POLICY TAG_SPI_SLEEP_FLOAT
#else
#define AK09940A_PWR TAG_NO_LINE
#define AK09940A_SLEEP_POLICY TAG_SPI_SLEEP_SAFE_IDLE
#endif

static const TagSpiDevice ak09940a_bus = {
    .controller = &tagSpi1DefaultController,
    .config = &tagSpiDefaultConfig,
    .cs = AK09940A_CS,
    .sck = AK09940A_SCK,
    .miso = AK09940A_MISO,
    .mosi = AK09940A_MOSI,
    .pwr = AK09940A_PWR,
    .sleep_policy = AK09940A_SLEEP_POLICY,
};

static const TagSpiBus ak09940a_register_bus = {
    .spi = SPI1,
    .cs = AK09940A_CS,
    .dummy = 0xff,
};

static const TagStSpiRegisterBus ak09940a_register_spi = {
    .bus = &ak09940a_register_bus,
    .read_mask = 0x80,
    .write_mask = 0x00,
};

static const TagRegisterBus ak09940a_registers = {
    .read_register = tagStSpiReadRegister,
    .write_register = tagStSpiWriteRegister,
    .context = &ak09940a_register_spi,
};

void magPowerOn(void)
{
  tagSpiDevicePowerOn(&ak09940a_bus);
}

void magPowerOff(void)
{
#if defined(LINE_MAG_RSTN)
  palClearLine(LINE_MAG_RSTN);
#endif
  tagSpiDevicePowerOff(&ak09940a_bus);
}

void magBusBegin(void)
{
  tagSpiBusBegin(&ak09940a_bus);

#if defined(LINE_MAG_RSTN)
  toOutput(LINE_MAG_RSTN);
  palSetLine(LINE_MAG_RSTN);
#endif
}

void magBusEnd(void)
{
  tagSpiBusEnd(&ak09940a_bus);
}

void magOn(void)
{
  magPowerOn();
  magBusBegin();
}

void magOff(void)
{
  magBusEnd();
  magPowerOff();
}

static void magSleepMilliseconds(int ms)
{
  stopMilliseconds(false, ms);
}

#if defined(LINE_MAG_TRG)
#define AK09940A_TRG LINE_MAG_TRG
#endif

#if defined(AK09940A_TRG)
static void magTriggerMode(bool output)
{
  if (output)
    toOutput(AK09940A_TRG);
  else
    toInput(AK09940A_TRG);
}

static void magTrigger(void)
{
  palSetLine(AK09940A_TRG);
  palClearLine(AK09940A_TRG);
}

static bool magDataReadyLine(void)
{
  return palReadLine(AK09940A_TRG) == PAL_HIGH;
}
#endif

static const TagMagDevice ak09940a_device = {
    .registers = &ak09940a_registers,
    .power_on = magPowerOn,
    .power_off = magPowerOff,
    .bus_begin = magBusBegin,
    .bus_end = magBusEnd,
    .sleep_ms = magSleepMilliseconds,
#if defined(AK09940A_TRG)
    .set_trigger_output = magTriggerMode,
    .trigger = magTrigger,
    .data_ready_line = magDataReadyLine,
#else
    .set_trigger_output = 0,
    .trigger = 0,
    .data_ready_line = 0,
#endif
};

const TagMagDevice *tagAk09940aDevice(void)
{
  return &ak09940a_device;
}
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

#ifdef LPS_USART
void lpsOn(void)
{
  toOutput(LINE_LPS_PWR);
  palSetLine(LINE_LPS_PWR);

  palSetLine(LINE_LPS_CS);
  toOutput(LINE_LPS_CS);

  toAlternate(LINE_LPS_SCK);
  toAlternate(LINE_LPS_TX);
  toAlternate(LINE_LPS_RX);

  tagUsart2SyncEnable(&tagUsart2SyncDefaultConfig);
}

void lpsOff(void)
{
  tagUsart2SyncDisable();
  toAnalog(LINE_LPS_SCK);
  toAnalog(LINE_LPS_TX);
  toAnalog(LINE_LPS_RX);
  toAnalog(LINE_LPS_CS);
  palClearLine(LINE_LPS_PWR);
}
#endif

#ifdef LPS_SPI
void lpsOn(void)
{

  /* grab the mutex */

  chBSemWait(&SPImutex);

  //toOutput(LINE_STEVAL_PWR);
  palSetLine(LINE_STEVAL_PWR);

  /* configure select line*/

  palSetLine(LINE_STEVAL_CS);
  toOutput(LINE_STEVAL_CS);

  /* configure SPI1   */

  toAlternate(LINE_STEVAL_SCK);
  toAlternate(LINE_STEVAL_MISO);
  toAlternate(LINE_STEVAL_MOSI);

  spiEnable();
}

void lpsOff(void)
{
  
  toAnalog(LINE_STEVAL_SCK);
  toAnalog(LINE_STEVAL_MOSI);
  toAnalog(LINE_STEVAL_MISO);
  toAnalog(LINE_STEVAL_CS);
  palClearLine(LINE_STEVAL_PWR);
  chBSemSignal(&SPImutex);
}

#endif

#if (defined(TAG_SENSOR_ACCEL_ADXL362) || defined(USE_ADXL367) || defined(USE_LIS2DU12)) && !defined(ACCEL_USART)
void accelSpiOn()
{
  /* grab the mutex */

  chBSemWait(&SPImutex);

  /* configure select line*/

  palSetLine(LINE_ACCEL_CS);
  toOutput(LINE_ACCEL_CS);

  /* configure SPI1   */

  toAlternate(LINE_ACCEL_SCK);
  toAlternate(LINE_ACCEL_MOSI);
  toAlternate(LINE_ACCEL_MISO);

  spiEnable();
}

void accelSpiOff()
{
  palSetLine(LINE_ACCEL_CS);
  spiDisable();

  //toInput(LINE_ACCEL_CS);
  toOutput(LINE_ACCEL_SCK);
  toOutput(LINE_ACCEL_MOSI);
  toInput(LINE_ACCEL_MISO);

  chBSemSignal(&SPImutex);
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

#if defined(TAG_FLASH_AT25XE) || defined(TAG_FLASH_MX25R)
  tagDeviceTablePrepareStandby(pState->state);
#elif defined(TAG_HAS_EXTERNAL_FLASH)
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

  tagDeviceTableApplyStandbyPulls();

#if defined(TAG_HAS_EXTERNAL_FLASH) && !defined(TAG_FLASH_AT25XE) && !defined(TAG_FLASH_MX25R)
  tagSpiDevicePrepareSleep(&flash_bus);
#endif

#ifdef TAG_SENSOR_MAG_AK09940A
#if defined(LINE_MAG_RSTN)
  tagEnableStandbyPulldown(LINE_MAG_RSTN);
#endif
  tagSpiDevicePrepareSleep(&ak09940a_bus);
#endif

  tagPrepareDevicesForStandby();

  // Pull up SCL and SDA on RTC

  tagEnableStandbyPullup(LINE_RTC_SCL);
  tagEnableStandbyPullup(LINE_RTC_SDA);

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
