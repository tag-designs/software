#ifndef TAG_CORE_ADC_H
#define TAG_CORE_ADC_H

#include <stdbool.h>
#include <stdint.h>

void adcVDD(uint16_t *vdd100, int16_t *temp10);
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

#endif
