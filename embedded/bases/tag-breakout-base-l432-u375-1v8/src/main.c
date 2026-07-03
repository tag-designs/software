/**************************************************************************
*      Copyright 2018  Geoffrey Brown                                     *
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
#include "board.h"
#include "dp_swd.h"

extern SerialUSBDriver SDU1;
extern const SerialUSBConfig serusbcfg;

volatile uint32_t vlipo100 = 180;

uint8_t bulkbuf[64];

static THD_WORKING_AREA(waBlinkThread, 256);
static THD_FUNCTION(BlinkThread, arg)
{
  (void)arg;

  while (true)
  {
    palToggleLine(LINE_LED_GREEN);
    chThdSleepMilliseconds(500);
  }
}

static void crsStart(void)
{
  RCC->APB1ENR1 |= RCC_APB1ENR1_CRSEN;

  CRS->CFGR = (2U << CRS_CFGR_SYNCSRC_Pos) |
              (34U << CRS_CFGR_FELIM_Pos) |
              48000U;
  CRS->CR |= CRS_CR_AUTOTRIMEN | CRS_CR_CEN;
}

int main(void)
{
  halInit();

  crsStart();

  palClearLine(LINE_SWDIO_DIR);
  palClearLine(LINE_TGT_RESET);
  palClearLine(LINE_TGT_SWCLK);
  palClearLine(LINE_TGT_SWDIO);
  palClearLine(LINE_UART_TX);
  toInput(LINE_TGT_SWDIO_IN);

  chSysInit();

  sduObjectInit(&SDU1);
  sduStart(&SDU1, &serusbcfg);

  usbDisconnectBus(&USBD1);
  chThdSleepMilliseconds(1500);
  usbStart(&USBD1, &usbcfg);
  usbConnectBus(&USBD1);

  chThdCreateStatic(waBlinkThread, sizeof(waBlinkThread),
                    NORMALPRIO, BlinkThread, NULL);

  while (true)
  {
    int n = BULK_Receive(bulkbuf, 64);
    if (n != 16)
    {
      chThdSleepMilliseconds(10);
    }
    else if (usbGetDriverStateI(&USBD1) == USB_ACTIVE)
    {
      palSetLine(LINE_UART_TX);
      stlink_eval(bulkbuf);
      palClearLine(LINE_UART_TX);
    }
  }
}
