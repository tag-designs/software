#include "hal.h"
#include "custom.h"
#include "tag.pb.h"
#include "config.h"
#include "core_runtime.h"
#include "core_sync.h"
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

#ifdef LPS_USART
static void usartEnable(void)
{
  rccEnableUSART2(true);
  rccResetUSART2();

  // Synchronous USART mode, MSB first. The LPS USART path uses USART2 as a
  // three-wire SPI-like bus for boards that did not route the pressure sensor
  // to the hardware SPI peripheral.
  USART2->BRR = 0x10;
  USART2->CR2 = USART_CR2_MSBFIRST | USART_CR2_CLKEN | USART_CR2_LBCL;
  USART2->CR3 = USART_CR3_OVRDIS | USART_CR3_ONEBIT;
  USART2->CR1 = USART_CR1_OVER8 | USART_CR1_TE | USART_CR1_RE | USART_CR1_UE;
  tagMarkUsart2On();
}

static void usartDisable(void)
{
  USART2->CR1 = 0;
  USART2->CR2 = 0;
  USART2->CR3 = 0;
  tagMarkUsart2Off();
}

void lpsOn(void)
{
  toOutput(LINE_LPS_PWR);
  palSetLine(LINE_LPS_PWR);

  palSetLine(LINE_LPS_CS);
  toOutput(LINE_LPS_CS);

  toAlternate(LINE_LPS_SCK);
  toAlternate(LINE_LPS_TX);
  toAlternate(LINE_LPS_RX);

  usartEnable();
}

void lpsOff(void)
{
  usartDisable();
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

#if defined(TAG_SENSOR_ACCEL_ADXL362) || defined(USE_ADXL367) || defined(USE_LIS2DU12)
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


#if defined(TAG_HAS_EXTERNAL_FLASH)
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

  // Pull up CS on ACCEL

#ifdef LINE_ACCEL_CS
  enableLinePullup(LINE_ACCEL_CS);
#endif

#ifdef TAG_HAS_EXTERNAL_FLASH
  enableLinePullup(LINE_FLASH_nCS);
  enableLinePulldown(LINE_FLASH_SCK);
  enableLinePulldown(LINE_FLASH_SCK);
  enableLinePulldown(LINE_FLASH_MOSI);
#endif

  // Pull up SCL and SDA on RTC

  enableLinePullup(LINE_RTC_SCL);
  enableLinePullup(LINE_RTC_SDA);

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
