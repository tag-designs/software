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

#ifdef SWAP_I2C
const I2CConfig rtci2cConfig = {
    .delay = delay,
    .sda = LINE_RTC_SCL,
    .scl = LINE_RTC_SDA
};
#else
const I2CConfig rtci2cConfig = {
    .delay = delay,
    .sda = LINE_RTC_SDA,
    .scl = LINE_RTC_SCL
};
#endif

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



void magOn(void){

  /* grab the mutex */

  chBSemWait(&SPImutex);

  palSetLine(LINE_AK_CS);

  /* configure SPI1   */

  toAlternate(LINE_LSM_CK);
  toAlternate(LINE_LPS_MISO);
  toAlternate(LINE_LSM_MOSI);

  spiEnable();

}

void magOff(void){

  palSetLine(LINE_AK_CS);
  spiDisable();
  toAnalog(LINE_LSM_CK);
  toAnalog(LINE_LSM_MOSI);
  toAnalog(LINE_LPS_MISO);
  chBSemSignal(&SPImutex);
}

void lpsOn(void){

  /* grab the mutex */

  chBSemWait(&SPImutex);
  palSetLine(LINE_LPS_CS);

  /* configure SPI1   */

  toAlternate(LINE_LSM_CK);
  toAlternate(LINE_LPS_MISO);
  toAlternate(LINE_LPS_MOSI);

  spiEnable();

}

void lpsOff(void){

  palSetLine(LINE_LPS_CS);
  spiDisable();
  toAnalog(LINE_LSM_CK);
  toAnalog(LINE_LPS_MOSI);
  toAnalog(LINE_LPS_MISO);
  chBSemSignal(&SPImutex);
}

void lsm6On(void){

  /* grab the mutex */

  chBSemWait(&SPImutex);
  palSetLine(LINE_LSM_CS);

  /* configure SPI1   */

  toAlternate(LINE_LSM_CK);
  toAlternate(LINE_LSM_MISO);
  toAlternate(LINE_LSM_MOSI);

  spiEnable();

}

void lsm6Off(void){

  palSetLine(LINE_LSM_CS);
  spiDisable();
  toAnalog(LINE_LSM_CK);
  toAnalog(LINE_LSM_MOSI);
  toAnalog(LINE_LSM_MISO);
  chBSemSignal(&SPImutex);
}

void FlashSpiOn(void)
{
  /* grab the mutex */

  chBSemWait(&SPImutex);
  palSetLine(LINE_MX_nCS);
  /* configure SPI1   */
  toAlternate(LINE_MX_SCK);
  toAlternate(LINE_MX_MOSI);
  toAlternate(LINE_MX_MISO);

  spiEnable();
}

void FlashSpiOff(void)
{
  palSetLine(LINE_MX_nCS);
  spiDisable();
  toAnalog(LINE_MX_SCK);
  toAnalog(LINE_MX_MOSI);
  toAnalog(LINE_MX_MISO);
  chBSemSignal(&SPImutex);
}


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


//#if defined(EXTERNAL_FLASH)
  // Make sure flash is in low power mode
  if ((pState->state == IDLE) ||
      (pState->state == ABORTED) ||
      (pState->state == FINISHED) ||
      (pState->state == EXCEPTION) ||
      (pState->state == HIBERNATING))
  {
    ExFlashPwrUp();
    //stopMilliseconds(true,1);
    ExFlashPwrDown();
  }

  // Make sure debug power is off
  __disable_irq();
  DBGMCU->CR = 0;
 
  // Mark the backup register.  Any reset at this point is ok
  // disable sram2 -- only works in standby, and pullup config

  CLEAR_BIT(PWR->CR3, PWR_CR3_RRS);             

  // Pull up SCL and SDA on RTC

  enableLinePullup(LINE_RTC_SCL);
  enableLinePullup(LINE_RTC_SDA);

  // Pull I/O pins

  enableLinePullup(LINE_LSM_CS);
  enableLinePullup(LINE_AK_CS);
  enableLinePullup(LINE_LPS_CS);
  enableLinePulldown(LINE_LSM_MOSI);
  enableLinePulldown(LINE_LSM_CK);

  enableLinePullup(LINE_MX_nCS);
  enableLinePulldown(LINE_MX_SCK);
  enableLinePulldown(LINE_MX_MOSI);



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
 

 #if 0
 //#if defined(LINE_ACCEL_INT)
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