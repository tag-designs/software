#include "hal.h"
#include "ch.h"


#ifndef US2RTC
#define US2RTC(freq, usec) (rtcnt_t)((((freq) + 999999UL) / 1000000UL) * (usec))
#endif

#define ADC_CCR_CKMODE_AHB_DIV1 (1 << 16)

static void adc_lld_vreg_on(void) {
  ADC1->CR = 0;
  ADC1->CR = ADC_CR_ADVREGEN;
  for (unsigned int i = 0; i < OSAL_US2RTC(STM32_HCLK, 20); i++) asm("NOP");
}

static void adc_lld_vreg_off(void) {
  ADC1->CR = 0;
  ADC1->CR = ADC_CR_DEEPPWD;
}

static void adc_lld_analog_on(void) {
  ADC1->CR |= ADC_CR_ADEN;
  while ((ADC1->ISR & ADC_ISR_ADRDY) == 0)
    ;
}

static void adc_lld_analog_off(void) {
  ADC1->CR |= ADC_CR_ADDIS;
  while ((ADC1->CR & ADC_CR_ADDIS) != 0)
    ;
}

static void adc_lld_calibrate(void) {
  ADC1->CR |= ADC_CR_ADCAL;
  while ((ADC1->CR & ADC_CR_ADCAL) != 0)
    ;
}

static void adc_lld_stop_adc(void) {
  if (ADC1->CR & ADC_CR_ADSTART) {
    ADC1->CR |= ADC_CR_ADSTP;
    while (ADC1->CR & ADC_CR_ADSTP)
      ;
  }
}

void adc1Start(void) {
  rccEnableADC123(true);
  rccResetADC123();
  ADC1_COMMON->CCR = ADC_CCR_CKMODE_AHB_DIV1;
  ADC1->IER = 0;
  adc_lld_vreg_on();
  adc_lld_calibrate();
  adc_lld_analog_on();
}

void adc1Stop(void) {
  adc_lld_stop_adc();
  adc_lld_analog_off();
  adc_lld_vreg_off();
  rccDisableADC123();
}

int adc1StartConversion(uint16_t channel, uint16_t delay) {
  // check that state is right
  // set delay -- SMPR1 or SMPR2
  // set grp  SQR1;
  // set CFGR
  if ((channel > 18) || (delay > 7)) return -1;

  // stop any outstanding conversion !
  adc_lld_stop_adc();
  // configure SMPR1
  if (channel < 10)
    ADC1->SMPR1 = (delay << (channel * 3));
  else
    ADC1->SMPR2 = (delay << ((channel - 9) * 3));
  // configure sequence
  ADC1->SQR1 = (channel << 6);
  // start conversion
  ADC1->CR |= ADC_CR_ADSTART;
  return 0;
}

bool adc1Eoc(void) { return ADC1->ISR & ADC_ISR_EOC; }

uint32_t adc1DR(void) { return ADC1->DR; }

void adc1StopConversion(void) { adc_lld_stop_adc(); }

void adc1EnableVREF(void) { ADC1_COMMON->CCR |= ADC_CCR_VREFEN; }

void adc1DisableVREF(void) { ADC1_COMMON->CCR &= ~ADC_CCR_VREFEN; }

void adc1EnableTS(void) { ADC1_COMMON->CCR |= ADC_CCR_TSEN; }

void adc1DisableTS(void) { ADC1_COMMON->CCR &= ~ADC_CCR_TSEN; }

