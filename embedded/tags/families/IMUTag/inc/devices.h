/**
 * @file devices.h
 * @brief IMUTag family device descriptors.
 * @author tag firmware authors
 * @date 2026-05-23
 */

#ifndef IMUTAGBREAKOUT_DEVICES_H
#define IMUTAGBREAKOUT_DEVICES_H

#include "sensor_io.h"
#include "lsm6dsv16x.h"
#include "lps.h"
#include "storage_flash.h"

#if defined(TAG_SENSOR_MAG_BMM350) && TAG_SENSOR_MAG_BMM350
#include "bmm350_tag.h"
/** BMM350 magnetometer descriptor for IMUTag-family targets that use BMM350. */
extern const TagBmm350Device tagImuTagBmm350Device;
#else
/** AK09940A register descriptor for IMUTag-family targets that use AK09940A. */
extern const TagRegisterDevice tagImuTagMagDevice;
#endif
/** LSM6DSV16X IMU descriptor shared by IMUTag-family collection code. */
extern const TagLsm6dsv16xDevice tagImuTagImuDevice;
/** Pressure-sensor descriptor shared by IMUTag-family collection code. */
extern const TagPressureDevice tagImuTagPressureDevice;
/** External flash descriptor used by IMUTag-family datalog storage. */
extern const TagStorageDevice tagExternalFlash;

/** @brief Configure the IMU external trigger output; divider 0 disables it. */
void tagImuTagSetTrigger(unsigned int divider);

#if defined(TAG_SENSOR_MAG_BMM350) && TAG_SENSOR_MAG_BMM350
/** Default magnetometer descriptor for shared BMM350 code. */
#define TAG_MAG_DEVICE (&tagImuTagBmm350Device)
#else
/** Default magnetometer descriptor for shared AK09940A code. */
#define TAG_MAG_DEVICE (&tagImuTagMagDevice)
#endif
/** Default IMU descriptor for shared LSM6DSV16X code. */
#define TAG_IMU_DEVICE (&tagImuTagImuDevice)
/** Default pressure descriptor for shared LPS22HH code. */
#define TAG_PRESSURE_DEVICE (&tagImuTagPressureDevice)
/** Default external-flash descriptor for shared storage code. */
#define TAG_EXTERNAL_FLASH (&tagExternalFlash)

#endif
