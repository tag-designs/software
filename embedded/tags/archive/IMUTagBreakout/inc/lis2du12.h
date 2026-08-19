/**
 * @file lis2du12.h
 * @brief Legacy IMUTagBreakout LIS2DU12 accelerometer API.
 * @author tag firmware authors
 * @date 2026-05-23
 */

#ifndef LIS2DU12_H
#define LIS2DU12_H

#include <stdbool.h>
#include <stdint.h>

/** @brief Legacy accelerometer operating modes. */
typedef enum {ACCEL_WAKEUP_MODE, 
            ACCEL_SAMPLE_50HZ_MODE, 
            ACCEL_SAMPLE_100HZ_MODE} lis2du12mode_t;

/** @brief Initialize the legacy accelerometer path. */
void accelInit(lis2du12mode_t mode);
/** @brief Deinitialize the legacy accelerometer path. */
void accelDeinit(void);
/** @brief Reset the legacy accelerometer path. */
void accelReset(void);
/** @brief Test the legacy accelerometer path. */
bool accelTest(void);
/** @brief Read one sample from the legacy accelerometer path. */
bool accelSample(uint8_t *);


#endif
