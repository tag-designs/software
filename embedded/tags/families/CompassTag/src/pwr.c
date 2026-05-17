#include "hal.h"

#include "ak09940a.h"
#include "app.h"
#include "external_flash.h"
#include "persistent.h"
#include "power.h"

/*
 * Shared CompassTag-family power and bus control.
 *
 * The three active CompassTag variants use the same RTC, magnetometer,
 * LIS2DU12 USART-style accelerometer interface, flash SPI interface, and
 * standby wakeup flow. Board files still provide the physical LINE_xxx names;
 * this family source describes how those lines are driven while active, off,
 * and asleep.
 */

static void delay(void)
{
  __NOP();
}

static const TagI2cDevice rtc_bus = {
    .driver = &I2CD1,
    .mutex = &I2Cmutex,
    .config = {
        .delay = delay,
#ifdef SWAP_I2C
        .sda = LINE_RTC_SCL,
        .scl = LINE_RTC_SDA,
#else
        .sda = LINE_RTC_SDA,
        .scl = LINE_RTC_SCL,
#endif
    },
    .sda = LINE_RTC_SDA,
    .scl = LINE_RTC_SCL,
    .pwr = TAG_NO_LINE,
};

#ifdef TAG_SENSOR_MAG_AK09940A
static const TagSpiDevice mag_bus = {
    .controller = &tagSpi1DefaultController,
    .mutex = &SPImutex,
    .cs = LINE_MAG_CS,
    .sck = LINE_MAG_SCK,
    .miso = LINE_MAG_MISO,
    .mosi = LINE_MAG_MOSI,
    .pwr = LINE_MAG_PWR,
    .off_policy = TAG_SPI_OFF_FLOAT,
    .sleep_policy = TAG_SPI_SLEEP_FLOAT,
};
#endif

#if defined(TAG_HAS_EXTERNAL_FLASH)
static const TagSpiDevice flash_bus = {
    .controller = &tagSpi1DefaultController,
    .mutex = &SPImutex,
    .cs = LINE_FLASH_nCS,
    .sck = LINE_FLASH_SCK,
    .miso = LINE_FLASH_MISO,
    .mosi = LINE_FLASH_MOSI,
    .pwr = TAG_NO_LINE,
    .off_policy = TAG_SPI_OFF_SAFE_IDLE,
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

static void usartEnable(void)
{
  rccEnableUSART2(true);
  rccResetUSART2();

  USART2->BRR = 0x10;
  USART2->CR2 = USART_CR2_MSBFIRST | USART_CR2_CLKEN | USART_CR2_LBCL;
  USART2->CR3 = USART_CR3_OVRDIS | USART_CR3_ONEBIT;
  USART2->CR1 = USART_CR1_OVER8 | USART_CR1_TE | USART_CR1_RE | USART_CR1_UE;
}

static void usartDisable(void)
{
  USART2->CR1 = 0;
  USART2->CR2 = 0;
  USART2->CR3 = 0;
}

#ifdef TAG_SENSOR_MAG_AK09940A
void magOn(void)
{
  tagSpiDeviceOn(&mag_bus);

  toOutput(LINE_MAG_RSTN);
  palSetLine(LINE_MAG_RSTN);
}

void magOff(void)
{
#ifdef COMPASS_TAG
  palClearLine(LINE_MAG_RSTN);
#endif
  tagSpiDeviceOff(&mag_bus);
}
#endif

#ifdef ACCEL_USART
void accelOn(void)
{
  palSetLine(LINE_ACCEL_CS);
  toOutput(LINE_ACCEL_CS);

  toAlternate(LINE_ACCEL_SCK);
  toAlternate(LINE_ACCEL_TX);
  toAlternate(LINE_ACCEL_RX);

  usartEnable();
}

void accelOff(void)
{
#ifdef COMPASS_TAG
  palSetLine(LINE_ACCEL_CS);
  usartDisable();
  toOutput(LINE_ACCEL_SCK);
  toOutput(LINE_ACCEL_TX);
  toAnalog(LINE_ACCEL_RX);
#else
  usartDisable();
  toAnalog(LINE_ACCEL_SCK);
  toAnalog(LINE_ACCEL_TX);
  toAnalog(LINE_ACCEL_RX);
#endif
}
#endif

#if defined(TAG_HAS_EXTERNAL_FLASH)
void FlashSpiOn(void)
{
  tagSpiDeviceOn(&flash_bus);
}

void FlashSpiOff(void)
{
  tagSpiDeviceOff(&flash_bus);
}
#endif

void godown(enum Sleep sleepmode)
{
  (void)sleepmode;

#if defined(TAG_HAS_EXTERNAL_FLASH)
  if ((pState->state == IDLE) ||
      (pState->state == ABORTED) ||
      (pState->state == FINISHED) ||
      (pState->state == EXCEPTION) ||
      (pState->state == HIBERNATING))
  {
    ExFlashPwrUp();
    stopMilliseconds(true, 1);
    ExFlashPwrDown();
  }
#endif

  __disable_irq();
  DBGMCU->CR = 0;

  CLEAR_BIT(PWR->CR3, PWR_CR3_RRS);

#ifdef TAG_HAS_EXTERNAL_FLASH
  tagSpiDevicePrepareSleep(&flash_bus);
#endif

  tagI2cDevicePrepareSleep(&rtc_bus);

#ifdef TAG_SENSOR_MAG_AK09940A
#ifdef COMPASS_TAG
  tagEnableStandbyPulldown(LINE_MAG_RSTN);
  tagEnableStandbyPulldown(LINE_MAG_CS);
  tagEnableStandbyPulldown(LINE_MAG_SCK);
  tagEnableStandbyPulldown(LINE_MAG_MOSI);
#endif
  tagEnableStandbyPulldown(LINE_MAG_PWR);
#endif

#ifdef ACCEL_USART
  tagEnableStandbyPullup(LINE_ACCEL_CS);
  tagEnableStandbyPulldown(LINE_ACCEL_TX);
  tagEnableStandbyPulldown(LINE_ACCEL_SCK);
#endif

  PWR->CR3 = 0x8000;

#if defined(LINE_ACCEL_INT)
  if (isActive)
  {
    SET_BIT(PWR->CR4, PWR_CR4_WP1);
  }
  else
  {
    CLEAR_BIT(PWR->CR4, PWR_CR4_WP1);
  }

  if (pState->state == RUNNING)
  {
    SET_BIT(PWR->CR3, PWR_CR3_EWUP1_Msk | PWR_CR3_EIWF_Msk);
    if (isActive != palReadLine(LINE_ACCEL_INT))
    {
      return;
    }
  }
  else
  {
    SET_BIT(PWR->CR3, PWR_CR3_EIWF_Msk);
  }
#else
  SET_BIT(PWR->CR3, PWR_CR3_EIWF_Msk);
#endif

  SET_BIT(PWR->CR3, PWR_CR3_APC);
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
