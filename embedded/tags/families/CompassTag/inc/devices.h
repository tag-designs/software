/**
 * @file devices.h
 * @brief CompassTag family device descriptors and board-control helpers.
 * @author tag firmware authors
 * @date 2026-05-23
 */

#ifndef COMPASSTAG_DEVICES_H
#define COMPASSTAG_DEVICES_H

#include <stdbool.h>

#include "ak09940a.h"
#include "lis2du12.h"
#include "storage_flash.h"

extern const TagRegisterDevice tagCompassTagAccelDevice;
extern const TagRegisterDevice tagCompassTagMagDevice;
extern const TagStorageDevice tagExternalFlash;

/** Default accelerometer register descriptor for shared LIS2DU12 code. */
#define TAG_ACCEL_DEVICE (&tagCompassTagAccelDevice)
/** Default magnetometer descriptor for shared AK09940A code. */
#define TAG_MAG_DEVICE (&tagCompassTagMagDevice)
/** Default external-flash descriptor for shared storage code. */
#define TAG_EXTERNAL_FLASH (&tagExternalFlash)

/** @brief Assert the magnetometer reset pin when present. */
void tagCompassMagResetAssert(void);
/** @brief Release the magnetometer reset pin when present. */
void tagCompassMagResetRelease(void);
/**
 * @brief Read the accelerometer wake line.
 *
 * @return true when the wake line is high.
 */
bool tagCompassAccelWakeActive(void);

#endif
