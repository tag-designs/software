/**
 * @file devices.h
 * @brief BitPresTag family device descriptors and device test bindings.
 * @author tag firmware authors
 * @date 2026-05-23
 */

#ifndef BITPRESTAG_DEVICES_H
#define BITPRESTAG_DEVICES_H

#include "ADXL362.h"
#include "lps27hhw.h"
#include "storage_flash.h"

extern const TagAdxl362Device tagBitPresTagAccelDevice;
extern const TagPressureDevice tagBitPresTagPressureDevice;
extern const TagStorageDevice tagExternalFlash;

/** Default accelerometer descriptor for shared ADXL362 code. */
#define TAG_ACCEL_DEVICE (&tagBitPresTagAccelDevice)
/** Default pressure descriptor for shared LPS27 code. */
#define TAG_PRESSURE_DEVICE (&tagBitPresTagPressureDevice)
/** Default external-flash descriptor for shared storage code. */
#define TAG_EXTERNAL_FLASH (&tagExternalFlash)

#endif
