/**
 * @file devices.h
 * @brief UIUCTag device descriptors and device test bindings.
 * @author tag firmware authors
 * @date 2026-08-30
 *
 * @details UIUCTag carries an ADXL362 and UIUCadxl367 an ADXL367 on the same
 *          footprint and bus. The accelerometer module a target's project.mk
 *          selects defines TAG_SENSOR_ACCEL_ADXL362 or TAG_SENSOR_ACCEL_ADXL367,
 *          and that picks the descriptor type here; everything else is shared.
 */

#ifndef UIUCTAG_DEVICES_H
#define UIUCTAG_DEVICES_H

#include "lps.h"
#include "storage_flash.h"

#if defined(TAG_SENSOR_ACCEL_ADXL367)
#include "ADXL367.h"
/** Accelerometer descriptor type for the fitted part (ADXL367). */
typedef TagAdxl367Device UiucTagAccelDevice;
#elif defined(TAG_SENSOR_ACCEL_ADXL362)
#include "ADXL362.h"
/** Accelerometer descriptor type for the fitted part (ADXL362). */
typedef TagAdxl362Device UiucTagAccelDevice;
#else
#error "UIUCTag needs sensor_accel_adxl362 or sensor_accel_adxl367 in TAG_MODULES"
#endif

extern const UiucTagAccelDevice uiucTagAccelDevice;
extern const TagPressureDevice uiucTagPressureDevice;
extern const TagStorageDevice tagExternalFlash;

/** Default accelerometer descriptor for the shared ADXL362/ADXL367 code. */
#define TAG_ACCEL_DEVICE (&uiucTagAccelDevice)
/** Default pressure descriptor for shared BMP581/BMP585 code. */
#define TAG_PRESSURE_DEVICE (&uiucTagPressureDevice)
/** Default external-flash descriptor for shared storage code. */
#define TAG_EXTERNAL_FLASH (&tagExternalFlash)

#endif
