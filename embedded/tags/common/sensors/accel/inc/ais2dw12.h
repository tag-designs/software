/**
 * @file ais2dw12.h
 * @brief Legacy AIS2DW12 sampling and self-test API.
 * @author tag firmware authors
 * @date 2026-05-23
 */

#ifndef _AIS2DW12_H
#define _AIS2DW12_H

/**
 * @brief Capture AIS2DW12 samples and compute RMS motion.
 *
 * @param[in] samples Number of samples to include in RMS calculation.
 * @param[out] rms RMS acceleration estimate.
 * @param[out] orientation Three-axis low-pass orientation sample.
 */
void ais_sample(int samples, int16_t *rms, int16_t orientation[3]);

/**
 * @brief Read the AIS2DW12 identity register.
 *
 * @return true when the expected device ID is present.
 */
bool ais2_test(void);

#endif
