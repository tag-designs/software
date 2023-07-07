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

#include "hal.h"
#include "stm32f0xx_ll_crs.h"
#include "usbcfg.h"
#include "app.h"
#include "board.h"

extern bool stlink_open;

#define  RCC_APB1ENR_CRSN     ((uint32_t)0x08000000U)

#define ADC_SMPR_SMP_1P5        0U  /**< @brief 14 cycles conversion time   */
#define ADC_SMPR_SMP_7P5        1U  /**< @brief 21 cycles conversion time.  */
#define ADC_SMPR_SMP_13P5       2U  /**< @brief 28 cycles conversion time.  */
#define ADC_SMPR_SMP_28P5       3U  /**< @brief 41 cycles conversion time.  */
#define ADC_SMPR_SMP_41P5       4U  /**< @brief 54 cycles conversion time.  */
#define ADC_SMPR_SMP_55P5       5U  /**< @brief 68 cycles conversion time.  */
#define ADC_SMPR_SMP_71P5       6U  /**< @brief 84 cycles conversion time.  */
#define ADC_SMPR_SMP_239P5      7U  /**< @brief 252 cycles conversion time. */


#define ADC_PA6_CHAN            6
#define ADC_PA4_CHAN            4
#define ADC_PA2_CHAN            2
#define ADC_PB1_CHAN            9
#define ADC_VREF_CHAN          17

const uint16_t *ADC_VREF_CAL = ((uint16_t *) 0x1FFFF7BA);

volatile uint32_t vlipo100;
static uint32_t vref100;


// Charger LED thread

static THD_WORKING_AREA(waThread1, 512);
static THD_FUNCTION(Thread1, arg) {

  (void)arg;

  //  chRegSetThreadName("charger");

  adc1Start();
  adc1EnableVREF();
  adc1StartConversion(ADC_VREF_CHAN,ADC_SMPR_SMP_239P5);
  while(adc1Eoc()==false)
    chThdYield();

  adc1DisableVREF();
  vref100 = ((*ADC_VREF_CAL)*330)/adc1DR();
  
  while (true) {

    chThdSleepMilliseconds(100);

    // don't read ADC if external power connected

    if (palReadLine(LINE_EXT_PWR)) {
      palSetLine(LINE_LED_RED);
      palSetLine(LINE_LED_GREEN);
      palClearLine(LINE_ADC_EN);
      palSetLine(LINE_TAG_VBAT_EN);
      palSetLine(LINE_TAG_3V3_EN);
      //vlipo100=330; // just so that stlink can connect
      //continue;
    } else {
      palClearLine(LINE_TAG_VBAT_EN);
      palClearLine(LINE_TAG_3V3_EN);
    }
    
    // Sample vlipo100
    // enable ADC input
    
    palSetLine(LINE_ADC_EN);  
    chThdSleepMilliseconds(10);
    
    // perform conversion
    
    //    adc1StartConversion(ADC_PB1_CHAN,ADC_SMPR_SMP_239P5);
//#ifdef BOARD_BITTAG_BASE_JLCPCB_V3
  if (palReadLine(LINE_EXT_PWR))
    adc1StartConversion(ADC_PA4_CHAN,ADC_SMPR_SMP_239P5);
  else
    adc1StartConversion(ADC_PA2_CHAN,ADC_SMPR_SMP_239P5);
//#else
//     adc1StartConversion(ADC_PA6_CHAN,ADC_SMPR_SMP_239P5);
//#endif
    while(adc1Eoc()==false)
      chThdYield();
    
    // disable ADC input

    palClearLine(LINE_ADC_EN); 

    vlipo100 = (vref100*adc1DR())/4096;

    if (vlipo100 < 325) 
      palClearLine(LINE_LED_RED);
    else
      palSetLine(LINE_LED_RED);

    if (vlipo100 > 315)
      palClearLine(LINE_LED_GREEN);
    else
      palSetLine(LINE_LED_GREEN);
  }
}

uint8_t bulkbuf[64];  // stlink input buffer

int main(void) {

  halInit();

  // Remap the USB pins PA11,PA12 onto the default PA9,PA10

  SYSCFG->CFGR1 |= SYSCFG_CFGR1_PA11_PA12_RMP;

  // Initialize clock recovery system

  rccEnableAPB1(RCC_APB1ENR_CRSEN,0);
  rccResetAPB1(RCC_APB1RSTR_CRSRST);

  LL_CRS_SetSyncDivider(LL_CRS_SYNC_DIV_1);
  LL_CRS_SetSyncPolarity(LL_CRS_SYNC_POLARITY_RISING);
  LL_CRS_SetSyncSignalSource(LL_CRS_SYNC_SOURCE_USB);
  LL_CRS_SetReloadCounter(__LL_CRS_CALC_CALCULATE_RELOADVALUE(48000000,
							      1000));
  LL_CRS_SetFreqErrorLimit(34);
  LL_CRS_SetHSI48SmoothTrimming(32);

  LL_CRS_EnableAutoTrimming();
  LL_CRS_EnableFreqErrorCounter();

  // disable swd line

  palSetLine(LINE_TGT_SWDIO_EN);  // polarity is reversed
  palClearLine(LINE_TGT_RESET);

  // Initialize system

  chSysInit();

  
  // Activate USB driver 

  usbDisconnectBus(&USBD1);
  chThdSleepMilliseconds(1500);
  usbStart(&USBD1, &usbcfg);
  usbConnectBus(&USBD1);

  // Create the charger thread

  chThdCreateStatic(waThread1, sizeof(waThread1), 
  		    NORMALPRIO, Thread1, NULL);

  // process stlink commands
  palSetLine(LINE_LED_GREEN);
  while (true) {
    int n = 0;
   /*  if (stlink_open) {
      palClearLine(LINE_LED_GREEN);   
    } else {
      palSetLine(LINE_LED_GREEN);   
    } */
    n = BULK_Receive(bulkbuf, 64);
    if (n != 16) {
      EPRINTF("received %d bytes expected 16\r\n", n);
      chThdSleepMilliseconds(10);
    }
    else {
      palSetLine(LINE_TGT_SWDIO_EN);
      stlink_eval(bulkbuf);
      palClearLine(LINE_TGT_SWDIO_EN);
    }
  }
}
