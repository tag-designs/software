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

#ifndef APP_H
#define APP_H

#undef USEEPRINTF
#ifdef USEEPRINTF
#define EPRINTF(...) chprintf((BaseSequentialStream *)&SD1,__VA_ARGS__)
#else
#define EPRINTF(...) while(0){}
#endif
extern void adcVDD(uint32_t *vdd100, uint32_t *vlipo100, int32_t *temp10);


// ADC

void adc1Start(void);
void adc1Stop(void);
int adc1StartConversion(uint16_t channel, uint16_t delay);
bool adc1Eoc(void);
uint32_t adc1DR(void);
void adc1StopConversion(void);
void adc1EnableVREF(void);
void adc1DisableVREF(void);
void adc1EnableTS(void);
void adc1DisableTS(void);


extern volatile uint32_t vlipo100;
int stlink_eval(uint8_t *buf);

//
// Pin modification functions
//
// We assume the alterate function number is set in board.h
// so mode configuration is a matter of setting a few bits
//

static inline void toInput(ioline_t line) {
  stm32_gpio_t *port = PAL_PORT(line);
  uint32_t pin = PAL_PAD(line);
  MODIFY_REG(port->MODER, 3 << (pin * 2), 0);
}

static inline void toOutput(ioline_t line) {
  stm32_gpio_t *port = PAL_PORT(line);
  uint32_t pin = PAL_PAD(line);
  MODIFY_REG(port->MODER, 3 << (pin * 2), 1 << (pin * 2));
}

static inline void toAlternate(ioline_t line) {
  stm32_gpio_t *port = PAL_PORT(line);
  uint32_t pin = PAL_PAD(line);
  MODIFY_REG(port->MODER, 3 << (pin * 2), 2 << (pin * 2));
}

static inline void toAnalog(ioline_t line) {
  stm32_gpio_t *port = PAL_PORT(line);
  uint32_t pin = PAL_PAD(line);
  MODIFY_REG(port->MODER, 3 << (pin * 2), 3 << (pin * 2));
}


#endif
