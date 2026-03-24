/**************************************************************************
*      Copyright 2018  Geoffrey Brown                                     *
*                                                                         *
*                                                                         *
*                                                                         *
* Licensed under the Apache License, Version 2.0 (the "License");         *
* you may not use this file except in compliance with the License.        *
* You may obtain a copy of the License at                                 *
*                                                                         *
*     http://www.apache.org/licenses/LICENSE-2.0                          *
*                                                                         *
* Unless required by applicable law or agreed to in writing, software     *
* distributed under the License is distributed on an "AS IS" BASIS,       *
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.*
* See the License for the specific language governing permissions and     *
* limitations under the License.                                          *
**************************************************************************/
#include "ch.h"
#include "hal.h"
#include "usbcfg.h"
#include "app.h"
#include "chprintf.h"
#include "board.h"
#include "dp_swd.h"

#define RCC_APB1ENR_CRSN ((uint32_t)0x08000000U)

#define ADC_SMPR_SMP_1P5 0U   /**< @brief 14 cycles conversion time   */
#define ADC_SMPR_SMP_7P5 1U   /**< @brief 21 cycles conversion time.  */
#define ADC_SMPR_SMP_13P5 2U  /**< @brief 28 cycles conversion time.  */
#define ADC_SMPR_SMP_28P5 3U  /**< @brief 41 cycles conversion time.  */
#define ADC_SMPR_SMP_41P5 4U  /**< @brief 54 cycles conversion time.  */
#define ADC_SMPR_SMP_55P5 5U  /**< @brief 68 cycles conversion time.  */
#define ADC_SMPR_SMP_71P5 6U  /**< @brief 84 cycles conversion time.  */
#define ADC_SMPR_SMP_239P5 7U /**< @brief 252 cycles conversion time. */

#define ADC_PA6_CHAN 6
#define ADC_PB1_CHAN 9
#define ADC_VREF_CHAN 17

const uint16_t *ADC_VREF_CAL = ((uint16_t *)0x1FFFF7BA);

volatile uint32_t vlipo100;
static uint32_t vref100;

static const SerialConfig sdcfg = {
  .speed = 115200, // e.g., 115200 baud
  .cr1 = 0,
  .cr2 = USART_CR2_STOP1_BITS | USART_CR2_LINEN,
  .cr3 = 0
};

// Charger LED thread

static THD_WORKING_AREA(waThread1, 512);
static THD_FUNCTION(Thread1, arg)
{

  (void)arg;
  int i = 0;

  //  chRegSetThreadName("charger");
  while (true)
  {
    chThdSleepMilliseconds(100);
    i++;
    if ((i%16) == 0) {
      //chprintf("\n%4d Open \r\n\n",i);
      //SWD_Open();
      //chprintf("\n%4d Close \r\n\n",i);
      //SWD_Close();

     // chprintf((BaseSequentialStream*)&SD1, "Ping %d %d\r\n", i, SWD_Open());
      // *(volatile uint8_t *)&SPI1->DR =  0xfA;
    }

    // don't read ADC if external power connected
#if 0
    if (!palReadLine(LINE_EXT_PWR))
    {
      //palClearLine(LINE_TAG_3V3_EN);
      vlipo100 = 330; // just so that stlink can connect
      continue;
    }
    else
    {
      //palSetLine(LINE_TAG_3V3_EN);
    }
#endif

    adc1Start();
    adc1EnableVREF();
    adc1StartConversion(ADC_VREF_CHAN, ADC_SMPR_SMP_239P5);
    while (adc1Eoc() == false)
      chThdYield();
    adc1DisableVREF();
    vref100 = ((*ADC_VREF_CAL) * 330) / adc1DR();

    // Sample vlipo100
    // enable ADC input

    chThdSleepMilliseconds(10);

    // perform conversion

    adc1StartConversion(ADC_PB1_CHAN,ADC_SMPR_SMP_239P5);
    //adc1StartConversion(ADC_PA6_CHAN, ADC_SMPR_SMP_239P5);
    while (adc1Eoc() == false)
      chThdYield();

    vlipo100 = (vref100 * adc1DR()) / 4096;
    if (vlipo100 < 200)
      vlipo100 = 330;
}
}


uint8_t bulkbuf[64]; // stlink input buffer

int main(void)
{

  halInit();

  // Initialize clock recovery system

  // 1. Enable CRS clock in APB1

  RCC->APBENR1 |= RCC_APBENR1_CRSEN;

  // 2. Configure CRS: Source = USB SOF (10), Sync polarity = Rising

  CRS->CFGR = (2 << CRS_CFGR_SYNCSRC_Pos) | (34 << CRS_CFGR_FELIM_Pos) | 48000;

  // 3. Enable Automatic Trimming and the Counter

  CRS->CR |= CRS_CR_AUTOTRIMEN | CRS_CR_CEN;

  // disable swd line

  //palSetLine(LINE_TGT_SWDIO_EN); // polarity is reversed
  palClearLine(LINE_TGT_RESET);

  // Initialize system

  chSysInit();

  // Activate USB driver

  usbDisconnectBus(&USBD1);
  chThdSleepMilliseconds(1500);
  usbStart(&USBD1, &usbcfg);
  usbConnectBus(&USBD1);

  /* debug */
  //sdStart(&SD2, &sdcfg);
  //chprintf("Main loop\n\r",0);

  /*

  palClearLine(LINE_TGT_SWCLK);
  palClearLine(LINE_TGT_SWDIO);

  toAlternate(LINE_TGT_SWCLK);
  toAlternate(LINE_TGT_SWDIO);

  rccEnableSPI1(0);
  rccResetSPI1();

  SPI1->CR1 = 0;
  SPI1->CR2 =  SPI_CR2_SSOE;

  SPI1->CR1 = SPI_CR1_LSBFIRST | (3<<3) | SPI_CR1_MSTR;
  SPI1->CR1 |= SPI_CR1_SPE;
  
*/

  // Create the charger thread

  chThdCreateStatic(waThread1, sizeof(waThread1),
                    NORMALPRIO, Thread1, NULL);

  // process stlink commands

  while (true)
  {
    int n = 0;
    n = BULK_Receive(bulkbuf, 64);
    if (n != 16)
    {
      EPRINTF("received %d bytes expected 16\r\n", n);
      chThdSleepMilliseconds(10);
    }
    else
    {
      //palSetLine(LINE_TGT_SWDIO_EN);
      stlink_eval(bulkbuf);
      //palClearLine(LINE_TGT_SWDIO_EN);
    }
  }
}
