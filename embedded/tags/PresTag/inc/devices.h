/**
 * @file devices.h
 * @brief PresTag device descriptors and device test bindings.
 * @author tag firmware authors
 * @date 2026-05-23
 */

#ifndef PRESTAG_DEVICES_H
#define PRESTAG_DEVICES_H

#include "lps27hhw.h"
#include "storage_flash.h"

extern const TagPressureDevice tagPresTagPressureDevice;
extern const TagStorageDevice tagExternalFlash;

/** Default pressure descriptor for shared LPS27 code. */
#define TAG_PRESSURE_DEVICE (&tagPresTagPressureDevice)
/** Default external-flash descriptor for shared storage code. */
#define TAG_EXTERNAL_FLASH (&tagExternalFlash)

/**
 * @brief Run the PresTag pressure-sensor presence test.
 *
 * @return true when the LPS27 device responds with the expected identity.
 */
static inline bool tagPressureTest(void)
{
  return lps27Test(TAG_PRESSURE_DEVICE);
}

#endif
