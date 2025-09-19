#include "hal.h"
#include "custom.h"
#include "app.h"
#include "config.h"
#include "persistent.h"
#include "external_flash.h"
#include "ak09940a.h"

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

// USART Device 

static void usartEnable(void)
{
  // Enable usart device
  
  rccEnableUSART2(true);
  rccResetUSART2();

   // enable maximum Baud rate (sysclk/8)
  USART2->BRR = 0x10;
  // enable clock and last bit clock pulse --swap
  // NOTE -- MSB_FIRST SPI expects this !!!!
  USART2->CR2 = USART_CR2_MSBFIRST | USART_CR2_CLKEN | USART_CR2_LBCL;
  // disable overrun 
  USART2->CR3 = USART_CR3_OVRDIS | USART_CR3_ONEBIT;
  // enable x8 oversampling, tx, rx, device
  USART2->CR1 = USART_CR1_OVER8 | USART_CR1_TE | USART_CR1_RE | USART_CR1_UE;
}

static void usartDisable(void)
{
  USART2->CR1 = 0;
  USART2->CR2 = 0;
  USART2->CR3 = 0;
}

#ifdef USE_AK09940A

void magOn(void){

  /* grab the mutex */

  chBSemWait(&SPImutex);

  toOutput(LINE_MAG_PWR);
  palSetLine(LINE_MAG_PWR);

  toOutput(LINE_MAG_RSTN);
  palSetLine(LINE_MAG_RSTN);

  /* configure select line*/

  palSetLine(LINE_MAG_CS);
  toOutput(LINE_MAG_CS);

  /* configure ready line */

  // not on breakout board

  /* configure SPI1   */

  toAlternate(LINE_MAG_SCK);
  toAlternate(LINE_MAG_MISO);
  toAlternate(LINE_MAG_MOSI);

  spiEnable();

}

void magOff(void){

  spiDisable();
  toAnalog(LINE_MAG_SCK);
  toAnalog(LINE_MAG_MOSI);
  toAnalog(LINE_MAG_MISO);
  toAnalog(LINE_MAG_CS);
  toAnalog(LINE_MAG_PWR);
  palClearLine(LINE_MAG_PWR);
  chBSemSignal(&SPImutex);
}

#endif

#ifdef ACCEL_USART

void accelOn(void)
{

  /* configure select line*/

  palSetLine(LINE_ACCEL_CS);
  toOutput(LINE_ACCEL_CS);

  /* configure USART1   */

  toAlternate(LINE_ACCEL_SCK);
  toAlternate(LINE_ACCEL_TX);
  toAlternate(LINE_ACCEL_RX);

  usartEnable();
}

void accelOff(void)
{
  usartDisable();
  toAnalog(LINE_ACCEL_SCK);
  toAnalog(LINE_ACCEL_TX);
  toAnalog(LINE_ACCEL_RX);
  toAnalog(LINE_ACCEL_CS);

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
 // enableLinePulldown(LINE_FLASH_MISO);
#endif

  // Pull up SCL and SDA on RTC

  enableLinePullup(LINE_RTC_SCL);
  enableLinePullup(LINE_RTC_SDA);

  // fully discharge Pressure Sensor capacitor

  //enableLinePulldown(LINE_STEVAL_PWR);

   // Disable wakeup source 1  why ?

   PWR->CR3 = 0x8000;  // reset wakeup register

   //CLEAR_BIT(PWR->CR3, PWR_CR3_EWUP1_Msk);

   // should disable PWR_EIWF_Msk  also ?   | PWR_CR3_EIWF_Msk
   // clear alarm flag ?
   // clear PWR_FLAG_WU ?
   // https://cpp.hotexamples.com/examples/-/-/HAL_PWR_EnterSTANDBYMode/cpp-hal_pwr_enterstandbymode-function-examples.html
 
   // set wakeup edge
 
 #if defined(LINE_ACCEL_INT)
   if (isActive)
   {
     SET_BIT(PWR->CR4, PWR_CR4_WP1); // falling edge detect
   }
   else
   {
     CLEAR_BIT(PWR->CR4, PWR_CR4_WP1); // rising edge detect
   }
 
   // enable wakeup on adxl input only in running state
 
   if (pState->state == RUNNING)
   {
     SET_BIT(PWR->CR3, PWR_CR3_EWUP1_Msk | PWR_CR3_EIWF_Msk);
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

 // turn on pullups/pulldowns

 SET_BIT(PWR->CR3, PWR_CR3_APC);

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