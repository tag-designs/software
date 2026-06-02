/**
 * @file devices.h
 * @brief IMUTagBreakout device descriptors.
 * @author tag firmware authors
 * @date 2026-05-23
 */

#ifndef IMUTAGBREAKOUT_DEVICES_H
#define IMUTAGBREAKOUT_DEVICES_H

#include "sensor_io.h"
#include "lps.h"
#include "storage_flash.h"

extern const TagRegisterDevice tagImuTagMagDevice;
extern const TagPressureDevice tagImuTagPressureDevice;
extern const TagStorageDevice tagExternalFlash;

/** Default magnetometer descriptor for shared AK09940A code. */
#define TAG_MAG_DEVICE (&tagImuTagMagDevice)
/** Default pressure descriptor for shared LPS22HH code. */
#define TAG_PRESSURE_DEVICE (&tagImuTagPressureDevice)
/** Default external-flash descriptor for shared storage code. */
#define TAG_EXTERNAL_FLASH (&tagExternalFlash)

#endif
