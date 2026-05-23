/**
 * @file lis2du12.h
 * @brief CompassTag LIS2DU12 accelerometer register-driver API.
 * @author tag firmware authors
 * @date 2026-05-23
 */

#ifndef LIS2DU12_H
#define LIS2DU12_H

#include "sensor_io.h"

#include <stdbool.h>
#include <stdint.h>

/** @brief CompassTag LIS2DU12 operating modes. */
typedef enum {ACCEL_WAKEUP_MODE, 
            ACCEL_SAMPLE_50HZ_MODE, 
            ACCEL_SAMPLE_100HZ_MODE} lis2du12mode_t;

/*
 * LIS2DU12 is a CompassTag-family register driver. Its public API takes the
 * configured register device directly; the tag devices.c file owns the
 * physical bus wiring, controller setup, and chip-select transaction details.
 */
/**
 * @brief Configure an LIS2DU12 descriptor for wakeup or sample operation.
 *
 * @param[in] device Register-device descriptor.
 * @param[in] mode Desired accelerometer mode.
 */
void lis2du12Init(const TagRegisterDevice *device, lis2du12mode_t mode);
/**
 * @brief Reset and power down an LIS2DU12 descriptor.
 *
 * @param[in] device Register-device descriptor.
 */
void lis2du12Deinit(const TagRegisterDevice *device);
/**
 * @brief Reset an LIS2DU12 descriptor through the family deinit path.
 *
 * @param[in] device Register-device descriptor.
 */
void lis2du12Reset(const TagRegisterDevice *device);
/**
 * @brief Check the LIS2DU12 identity register.
 *
 * @param[in] device Register-device descriptor.
 * @return true when the expected identity is present.
 */
bool lis2du12Test(const TagRegisterDevice *device);
/**
 * @brief Read one accelerometer sample when data is ready.
 *
 * @param[in] device Register-device descriptor.
 * @param[out] data Destination for six raw sample bytes.
 * @return true when a fresh sample was read.
 */
bool lis2du12Sample(const TagRegisterDevice *device, uint8_t *data);
/**
 * @brief Return the tag-selected LIS2DU12 descriptor.
 *
 * @return Default accelerometer register descriptor.
 */
const TagRegisterDevice *tagLis2du12Device(void);

/** @brief Initialize the default accelerometer descriptor. */
void accelInit(lis2du12mode_t mode);
/** @brief Deinitialize the default accelerometer descriptor. */
void accelDeinit(void);
/** @brief Reset the default accelerometer descriptor. */
void accelReset(void);
/** @brief Test the default accelerometer descriptor. */
bool accelTest(void);
/** @brief Sample the default accelerometer descriptor. */
bool accelSample(uint8_t *);


#endif
