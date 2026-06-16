/**
 * @file devices.h
 * @brief BitTagNG family device descriptors and device test bindings.
 * @author tag firmware authors
 * @date 2026-06-16
 */

#ifndef BITTAGNG_DEVICES_H
#define BITTAGNG_DEVICES_H

#include "ADXL367.h"
#include "storage_flash.h"

extern const TagAdxl367Device tagBitTagNGAccelDevice;
extern const TagStorageDevice tagExternalFlash;

/** Default accelerometer descriptor for shared ADXL367 code. */
#define TAG_ACCEL_DEVICE (&tagBitTagNGAccelDevice)
/** Default external-flash descriptor for shared storage code. */
#define TAG_EXTERNAL_FLASH (&tagExternalFlash)

#endif
