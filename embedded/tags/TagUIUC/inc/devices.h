/**
 * @file devices.h
 * @brief TagUIUC device descriptors and device test bindings.
 * @author tag firmware authors
 * @date 2026-08-30
 */

#ifndef TAGUIUC_DEVICES_H
#define TAGUIUC_DEVICES_H

#include "ADXL367.h"
#include "lps.h"
#include "storage_flash.h"

extern const TagAdxl367Device tagUIUCAccelDevice;
extern const TagPressureDevice tagUIUCPressureDevice;
extern const TagStorageDevice tagExternalFlash;

/** Default accelerometer descriptor for shared ADXL367 code. */
#define TAG_ACCEL_DEVICE (&tagUIUCAccelDevice)
/** Default pressure descriptor for shared BMP581/BMP585 code. */
#define TAG_PRESSURE_DEVICE (&tagUIUCPressureDevice)
/** Default external-flash descriptor for shared storage code. */
#define TAG_EXTERNAL_FLASH (&tagExternalFlash)

#endif
