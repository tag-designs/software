/**
 * @file lis2dtw12.h
 * @brief Legacy LIS2DTW12 sampling and self-test API.
 * @author tag firmware authors
 * @date 2026-05-23
 */

#ifndef _LIS2DTW12_H
#define _LIS2DTW12_H

/**
 * @brief Capture LIS2DTW12 samples and compute RMS motion.
 *
 * @param[in] samples Number of samples to include in RMS calculation.
 * @param[out] rms RMS acceleration estimate.
 * @param[out] orientation Three-axis low-pass orientation sample.
 */
void lis2_sample(int samples, int16_t *rms, int16_t orientation[3]);

/**
 * @brief Read the LIS2DTW12 identity register.
 *
 * @return true when the expected device ID is present.
 */
bool lis2_test(void);

#endif
