#include "hal.h"

#include "ak09940a.h"
#include "app.h"
#include "external_flash.h"
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
    .sleep_policy = TAG_SPI_SLEEP_FLOAT,
};

/*
 * Register-transfer descriptor for the AK09940A driver. `mag_bus` above owns
 * power, mutex, pin modes, and SPI controller lifetime; this smaller
 * descriptor is only the selected SPI registers and chip select used while the
 * bus is already enabled.
 */
static const TagSpiBus mag_register_bus = {
    .spi = SPI1,
    .cs = LINE_MAG_CS,
    .dummy = 0xff,
};

static const TagStSpiRegisterBus mag_register_spi = {
    .bus = &mag_register_bus,
    .read_mask = 0x80,
    .write_mask = 0x00,
};

static const TagRegisterBus mag_registers = {
    .read_register = tagStSpiReadRegister,
    .write_register = tagStSpiWriteRegister,
    .context = &mag_register_spi,
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

#ifdef TAG_SENSOR_MAG_AK09940A
void magPowerOn(void)
{
  tagSpiDevicePowerOn(&mag_bus);
}

void magPowerOff(void)
{
#ifdef COMPASS_TAG
  palClearLine(LINE_MAG_RSTN);
#endif
  tagSpiDevicePowerOff(&mag_bus);
}

void magBusBegin(void)
{
  tagSpiBusBegin(&mag_bus);

  toOutput(LINE_MAG_RSTN);
  palSetLine(LINE_MAG_RSTN);
}

void magBusEnd(void)
{
  tagSpiBusEnd(&mag_bus);
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
static void magTriggerMode(bool output)
{
  if (output)
    toOutput(LINE_MAG_TRG);
  else
    toInput(LINE_MAG_TRG);
}

static void magTrigger(void)
{
  palSetLine(LINE_MAG_TRG);
  palClearLine(LINE_MAG_TRG);
}

static bool magDataReadyLine(void)
{
  return palReadLine(LINE_MAG_TRG) == PAL_HIGH;
}
#endif

static const TagMagDevice compass_tag_mag = {
    .registers = &mag_registers,
    .power_on = magPowerOn,
    .power_off = magPowerOff,
    .bus_begin = magBusBegin,
    .bus_end = magBusEnd,
    .sleep_ms = magSleepMilliseconds,
#if defined(LINE_MAG_TRG)
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
  return &compass_tag_mag;
}
#endif

#if defined(TAG_HAS_EXTERNAL_FLASH)
void FlashSpiOn(void)
{
  tagSpiBusBegin(&flash_bus);
}

void FlashSpiOff(void)
{
  tagSpiBusEnd(&flash_bus);
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
