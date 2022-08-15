#include "hal.h"
#include "custom.h"
#include "app.h"
#include "config.h"
#include "persistent.h"
#include "external_flash.h"
#include "lps.h"

/*
 * I2C Devices
 */

const I2CConfig i2cfg1 = {
    0x509, //0, //0x0509, //0x00200B28, //24Mhz, 400khz, 20ns rise ,20ns fall
    //  0x2010091A, // computed by CubeMX
    0,
    0,
};

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
}

static void spiDisable(void)
{
  SPI1->CR1 = 0;
  SPI1->CR2 = 0;
}

void rtcOn(void)
{
  chBSemWait(&I2Cmutex); //i2cAcquireBus(&I2CD1);
  toAlternate(LINE_RTC_SCL);
  toAlternate(LINE_RTC_SDA);
  i2cStart(&I2CD1, &i2cfg1);
}

void rtcOff(void)
{
  i2cStop(&I2CD1);
  toOutput(LINE_RTC_SCL);
  toOutput(LINE_RTC_SDA);
  chBSemSignal(&I2Cmutex); //i2cReleaseBus(&I2CD1);
}

#ifdef LPS_I2C
void lpsOn(void)
{
  chBSemWait(&I2Cmutex);
  toOutput(LINE_STEVAL_PWR);
  palSetLine(LINE_STEVAL_PWR);
  palSetLine(LINE_STEVAL_CS);
  toOutput(LINE_STEVAL_CS);
  palClearLine(LINE_STEVAL_MISO);
  toOutput(LINE_STEVAL_MISO);
  //toInput(LINE_INT1);
  toAlternate(LINE_STEVAL_SCL);
  toAlternate(LINE_STEVAL_SDA);
  i2cStart(&I2CD1, &i2cfg1);
}

void lpsOff(void)
{
  i2cStop(&I2CD1);
  //toAnalog(LINE_INT1);
  toAnalog(LINE_STEVAL_SCL);
  toAnalog(LINE_STEVAL_SDA);
  toAnalog(LINE_STEVAL_CS);
  palClearLine(LINE_STEVAL_PWR);
  toAnalog(LINE_STEVAL_PWR);
  chBSemSignal(&I2Cmutex);
}
#endif

#ifdef LPS_SPI
void lpsOn(void)
{
  /* grab the mutex */

  chBSemWait(&SPImutex);

  /* configure select line*/

  palSetLine(LINE_STEVAL_PWR);
  palSetLine(LINE_STEVAL_CS);
  toOutput(LINE_STEVAL_PWR);
  toOutput(LINE_STEVAL_CS);

  /* configure SPI1   */

  toAlternate(LINE_STEVAL_SCK);
  toAlternate(LINE_STEVAL_MOSI);
  toAlternate(LINE_STEVAL_MISO);

  spiEnable();
}

void lpsOff(void)
{
  palSetLine(LINE_STEVAL_CS);
  spiDisable();
  toAnalog(LINE_STEVAL_SCK);
  toAnalog(LINE_STEVAL_MOSI);
  toAnalog(LINE_STEVAL_MISO);
  toAnalog(LINE_STEVAL_CS);
  toAnalog(LINE_STEVAL_PWR);
  chBSemSignal(&SPImutex);
}
#endif

#if defined(USE_ADXL362)
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

#if defined(EXTERNAL_FLASH)
void FlashSpiOn(void)
{
  /* grab the mutex */

  chBSemWait(&SPImutex);
  palSetLine(LINE_FLASH_nCS);
  /* configure SPI1   */
  toAlternate(LINE_FLASH_SCK);
  toAlternate(LINE_FLASH_MOSI);
  toAlternate(LINE_FLASH_MISO);

  spiEnable();
}

void FlashSpiOff(void)
{
  palSetLine(LINE_FLASH_nCS);
  spiDisable();
  toOutput(LINE_FLASH_SCK);
  toOutput(LINE_FLASH_MOSI);
  toAnalog(LINE_FLASH_MISO);
  chBSemSignal(&SPImutex);
}
#endif

/*
 * Standby/Shutdown modes
 */

static void enableLinePullup(ioline_t line)
{
  if ((PAL_PAD(line) != (14)) && (PAL_PORT(line) == GPIOA))
    SET_BIT(PWR->PUCRA, 1 << PAL_PAD(line));
  if (PAL_PORT(line) == GPIOB)
    SET_BIT(PWR->PUCRB, 1 << PAL_PAD(line));
}

static void enableLinePulldown(ioline_t line)
{
  if ((PAL_PAD(line) != (13)) && (PAL_PAD(line) != (15)) && (PAL_PORT(line) == GPIOA))
    SET_BIT(PWR->PDCRA, 1 << PAL_PAD(line));
  if (PAL_PORT(line) == GPIOB)
    SET_BIT(PWR->PDCRB, 1 << PAL_PAD(line));
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

#define STM32_EXT_RTC_ALARM_LINE (1U << 18)
#define STM32_EXT_RTC_TIMER_LINE (1U << 20)

void godown(enum Sleep sleepmode)
{
  (void) sleepmode;

#if defined(EXTERNAL_FLASH)
  // Make sure flash is in low power mode
  if ((pState->state == IDLE) ||
      (pState->state == ABORTED) ||
      (pState->state == FINISHED) ||
      (pState->state == HIBERNATING))
  {
    ExFlashPwrUp();
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
  enableLinePullup(LINE_ACCEL_CS);
#endif

#ifdef EXTERNAL_FLASH
  enableLinePullup(LINE_FLASH_nCS);
  enableLinePulldown(LINE_FLASH_SCK);
  enableLinePulldown(LINE_FLASH_MOSI);
  enableLinePulldown(LINE_FLASH_MISO);
#endif

  // Pull up SCL and SDA on RTC

  enableLinePullup(LINE_RTC_SCL);
  enableLinePullup(LINE_RTC_SDA);

  // turn on pullups

  SET_BIT(PWR->CR3, PWR_CR3_APC);

  // Disable wakeup source 4  why ?

  CLEAR_BIT(PWR->CR3, PWR_CR3_EWUP4_Msk);

  // should disable PWR_EIWF_Msk  also ?   | PWR_CR3_EIWF_Msk
  // clear alarm flag ?
  // clear PWR_FLAG_WU ?
  // https://cpp.hotexamples.com/examples/-/-/HAL_PWR_EnterSTANDBYMode/cpp-hal_pwr_enterstandbymode-function-examples.html

  // set wakeup edge

#if defined(LINE_ACCEL_INT)
  if (isActive)
  {
    SET_BIT(PWR->CR4, PWR_CR4_WP4); // falling edge detect
  }
  else
  {
    CLEAR_BIT(PWR->CR4, PWR_CR4_WP4); // rising edge detect
  }

  // enable wakeup on adxl input only in running state

  if (pState->state == RUNNING)
  {
    SET_BIT(PWR->CR3, PWR_CR3_EWUP4_Msk | PWR_CR3_EIWF_Msk);
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

  // Set low power mode

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
  __disable_irq();
  pState->state = EXCEPTION;

  // Make sure debug power is off

  DBGMCU->CR = 0;
  CLEAR_BIT(PWR->CR3, PWR_CR3_EWUP4_Msk | PWR_CR3_EIWF_Msk);
  MODIFY_REG(PWR->CR1, PWR_CR1_LPMS, PWR_CR1_LPMS_SHUTDOWN);

  // Set SLEEPDEEP bit of Cortex System Control Register

  SET_BIT(SCB->SCR, ((uint32_t)SCB_SCR_SLEEPDEEP_Msk));

  __DSB();
  __WFI();
}