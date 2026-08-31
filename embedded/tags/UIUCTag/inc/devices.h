/**
 * @file devices.h
 * @brief UIUCTag device descriptors and device test bindings.
 * @author tag firmware authors
 * @date 2026-08-30
 */

#ifndef UIUCTAG_DEVICES_H
#define UIUCTAG_DEVICES_H

#include "ADXL367.h"
#include "lps.h"
#include "storage_flash.h"

extern const TagAdxl367Device uiucTagAccelDevice;
extern const TagPressureDevice uiucTagPressureDevice;
extern const TagStorageDevice tagExternalFlash;

/** Default accelerometer descriptor for shared ADXL367 code. */
#define TAG_ACCEL_DEVICE (&uiucTagAccelDevice)
/** Default pressure descriptor for shared BMP581/BMP585 code. */
#define TAG_PRESSURE_DEVICE (&uiucTagPressureDevice)
/** Default external-flash descriptor for shared storage code. */
#define TAG_EXTERNAL_FLASH (&tagExternalFlash)

#endif
