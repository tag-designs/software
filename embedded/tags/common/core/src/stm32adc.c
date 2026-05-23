/**
 * @file stm32adc.c
 * @brief STM32 ADC1 lifecycle and one-shot conversion support.
 * @author tag firmware authors
 * @date 2026-05-23
 */

#include "hal.h"
#include "ch.h"

#include "adc.h"

#ifndef US2RTC
#define US2RTC(freq, usec) (rtcnt_t)((((freq) + 999999UL) / 1000000UL) * (usec))
#endif

#define ADC_CCR_CKMODE_AHB_DIV1 (1 << 16)

/** @name ADC analog power sequencing
 * Helpers that keep the STM32 ADC regulator, analog core, and calibration
 * sequence in one place so callers do not accidentally sample from a partially
 * powered ADC.
 * @{
 */
/**
 * @brief Bring the ADC voltage regulator out of deep power-down.
 */
static void adc_lld_vreg_on(void) {
  ADC1->CR = 0;
  ADC1->CR = ADC_CR_ADVREGEN;
  for (unsigned int i = 0; i < OSAL_US2RTC(STM32_HCLK, 20); i++) asm("NOP");
}

/**
 * @brief Return the ADC voltage regulator to deep power-down.
 */
static void adc_lld_vreg_off(void) {
  ADC1->CR = 0;
  ADC1->CR = ADC_CR_DEEPPWD;
}

/**
 * @brief Enable the ADC analog core and wait until it is ready to sample.
 */
static void adc_lld_analog_on(void) {
  ADC1->CR |= ADC_CR_ADEN;
  while ((ADC1->ISR & ADC_ISR_ADRDY) == 0)
    ;
}

/**
 * @brief Disable the ADC analog core after conversions are complete.
 */
static void adc_lld_analog_off(void) {
  ADC1->CR |= ADC_CR_ADDIS;
  while ((ADC1->CR & ADC_CR_ADDIS) != 0)
    ;
}

/**
 * @brief Run the STM32 self-calibration step required before accurate sampling.
 */
static void adc_lld_calibrate(void) {
  ADC1->CR |= ADC_CR_ADCAL;
  while ((ADC1->CR & ADC_CR_ADCAL) != 0)
    ;
}

/**
 * @brief Stop an active conversion before reconfiguring or powering down ADC1.
 */
static void adc_lld_stop_adc(void) {
  if (ADC1->CR & ADC_CR_ADSTART) {
    ADC1->CR |= ADC_CR_ADSTP;
    while (ADC1->CR & ADC_CR_ADSTP)
      ;
  }
}
/** @} */

/** @name ADC1 lifecycle
 * Public entry points used by core measurement code to power ADC1 only around
 * the short windows where a conversion is needed.
 * @{
 */
/**
 * @brief Enable, calibrate, and ready ADC1 for conversions.
 */
void adc1Start(void) {
  rccEnableADC123(true);
  rccResetADC123();
  ADC1_COMMON->CCR = ADC_CCR_CKMODE_AHB_DIV1;
  ADC1->IER = 0;
  adc_lld_vreg_on();
  adc_lld_calibrate();
  adc_lld_analog_on();
}

/**
 * @brief Stop ADC1 and remove its analog and clock power.
 */
void adc1Stop(void) {
  adc_lld_stop_adc();
  adc_lld_analog_off();
  adc_lld_vreg_off();
  rccDisableADC123();
}

/**
 * @brief Configure and start a single ADC1 conversion.
 *
 * @param[in] channel STM32 ADC channel number to sample.
 * @param[in] delay STM32 sample-time selector for the channel.
 * @return 0 when the conversion was started, or -1 for an invalid channel or delay.
 */
int adc1StartConversion(uint16_t channel, uint16_t delay) {
  if ((channel > 18) || (delay > 7)) return -1;

  adc_lld_stop_adc();
  if (channel < 10)
    ADC1->SMPR1 = (delay << (channel * 3));
  else
    ADC1->SMPR2 = (delay << ((channel - 9) * 3));
  ADC1->SQR1 = (channel << 6);
  ADC1->CR |= ADC_CR_ADSTART;
  return 0;
}

/**
 * @brief Report whether the active ADC1 conversion has produced a sample.
 *
 * @return true when ADC1 has a completed conversion result.
 */
bool adc1Eoc(void) { return ADC1->ISR & ADC_ISR_EOC; }

/**
 * @brief Read the most recent ADC1 conversion result.
 *
 * @return Raw ADC data-register value.
 */
uint32_t adc1DR(void) { return ADC1->DR; }

/**
 * @brief Cancel any in-flight ADC1 conversion.
 */
void adc1StopConversion(void) { adc_lld_stop_adc(); }
/** @} */

/** @name Internal ADC channels
 * Enable or disable STM32 internal sources that feed board status measurements.
 * @{
 */
/**
 * @brief Enable the internal voltage-reference channel.
 */
void adc1EnableVREF(void) { ADC1_COMMON->CCR |= ADC_CCR_VREFEN; }

/**
 * @brief Disable the internal voltage-reference channel.
 */
void adc1DisableVREF(void) { ADC1_COMMON->CCR &= ~ADC_CCR_VREFEN; }

/**
 * @brief Enable the internal temperature-sensor channel.
 */
void adc1EnableTS(void) { ADC1_COMMON->CCR |= ADC_CCR_TSEN; }

/**
 * @brief Disable the internal temperature-sensor channel.
 */
void adc1DisableTS(void) { ADC1_COMMON->CCR &= ~ADC_CCR_TSEN; }
/** @} */
