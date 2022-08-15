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

static void delay(void){
  __NOP();
}

// I2C interface to rtc

const I2CConfig rtci2cConfig = {
    .delay = delay,
    .sda = LINE_RTC_SDA,
    .scl = LINE_RTC_SCL
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
  toAnalog(LINE_FLASH_SCK);
  toAnalog(LINE_FLASH_MOSI);
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

void godown(enum Sleep sleepmode)
{
  (void) sleepmode;

#if defined(EXTERNAL_FLASH)
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

#ifdef EXTERNAL_FLASH
  enableLinePullup(LINE_FLASH_nCS);
  enableLinePulldown(LINE_FLASH_SCK);
  enableLinePulldown(LINE_FLASH_MOSI);
#endif

  // Pull up SCL and SDA on RTC

  enableLinePullup(LINE_RTC_SCL);
  enableLinePullup(LINE_RTC_SDA);

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