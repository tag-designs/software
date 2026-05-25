/**
 * @file adc.h
 * @brief Public ADC helpers for battery, temperature, and raw ADC1 reads.
 * @author tag firmware authors
 * @date 2026-05-23
 */

#ifndef TAG_CORE_ADC_H
#define TAG_CORE_ADC_H

#include <stdbool.h>
#include <stdint.h>

/** @name Board measurement helpers
 * Functions that turn ADC readings into tag-level voltage and temperature
 * values used by status reports and telemetry.
 * @{
 */
/**
 * @brief Initialize ADC module runtime state.
 */
void adcInit(void);

/**
 * @brief Measure the MCU supply voltage and internal temperature.
 *
 * This helper exists so status/reporting code can ask for calibrated board
 * health values without knowing the STM32 ADC reference-channel sequence.
 *
 * @param[out] vdd100 Supply voltage in hundredths of a volt.
 * @param[out] temp10 Die temperature in tenths of a degree Celsius.
 */
void adcVDD(uint16_t *vdd100, int16_t *temp10);
/** @} */

/** @name ADC1 lifecycle
 * Functions that make the low-level ADC peripheral available for one-shot
 * conversions and then return it to a low-power state.
 * @{
 */
/**
 * @brief Enable, calibrate, and ready ADC1 for conversions.
 */
void adc1Start(void);

/**
 * @brief Stop ADC1 and remove its analog and clock power.
 */
void adc1Stop(void);

/**
 * @brief Configure and start a single ADC1 conversion.
 *
 * @param[in] channel STM32 ADC channel number to sample.
 * @param[in] delay STM32 sample-time selector for the channel.
 * @return 0 when the conversion was started, or -1 for an invalid channel or delay.
 */
int adc1StartConversion(uint16_t channel, uint16_t delay);

/**
 * @brief Report whether the active ADC1 conversion has produced a sample.
 *
 * @return true when ADC1 has a completed conversion result.
 */
bool adc1Eoc(void);

/**
 * @brief Read the most recent ADC1 conversion result.
 *
 * @return Raw ADC data-register value.
 */
uint32_t adc1DR(void);

/**
 * @brief Cancel any in-flight ADC1 conversion.
 */
void adc1StopConversion(void);
/** @} */

/** @name Internal ADC channels
 * Functions that enable or disable internal STM32 sources shared by the ADC
 * status measurement code.
 * @{
 */
/**
 * @brief Enable the internal voltage-reference channel.
 */
void adc1EnableVREF(void);

/**
 * @brief Disable the internal voltage-reference channel.
 */
void adc1DisableVREF(void);

/**
 * @brief Enable the internal temperature-sensor channel.
 */
void adc1EnableTS(void);

/**
 * @brief Disable the internal temperature-sensor channel.
 */
void adc1DisableTS(void);
/** @} */

#endif
