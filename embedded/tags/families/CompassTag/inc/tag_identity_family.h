/**
 * @file    tag_identity_family.h
 * @brief   CompassTag family facts for the tag identity record.
 *
 * @details Replaces common/core/inc/tag_identity_family.h by basename; see
 *          that file for the macros. Header i owns external bytes
 *          [i*380, (i+1)*380): DATALOG_SAMPLES blocks, each SAMPLES_PER_BLOCK
 *          RawSensorData samples (6 x int16) followed by one uint16 activity
 *          word packing ACTIVITY_BITS_PER_SAMPLE bits per sample. A block
 *          whose activity word is 0xFFFF is unfinished and skipped.
 *
 *          Scale slots, in this order, as data_logAck() applies them:
 *          0. header voltage: vdd100 * 0.01;
 *          1. header core temperature: temp10 * 0.1 (signed);
 *          2. acceleration: raw * 0.976 (mg);
 *          3. magnetic field: raw * 0.04 (uT);
 *          4. activity: field * 100 / COMPASS_SAMPLE_PERIOD_S (percent).
 *
 *          Calibration slots are {int32 timestamp; 13 floats}, 56 bytes, in
 *          one 2048-byte table; sensors.c asserts the size.
 */

#ifndef TAG_IDENTITY_FAMILY_H
#define TAG_IDENTITY_FAMILY_H

/** @brief Firmware family name. */
#define TAG_IDENTITY_FAMILY "CompassTag"
/** @brief Host sqlitelog decoder that reads this family's data log. */
#define TAG_IDENTITY_DECODER "compasstag"
/** @brief Version of the data layout; bump when it changes. */
#define TAG_IDENTITY_DATA_LAYOUT_VERSION 1U
/** @brief How a header finds its external data (TAG_IDENTITY_MAP_*). */
#define TAG_IDENTITY_MAPPING TAG_IDENTITY_MAP_STRIDE
/** @brief Bytes per internal header. */
#define TAG_IDENTITY_DATA_HEADER_SIZE sizeof(t_DataHeader)
/** @brief Bytes of external data per header or page. */
#define TAG_IDENTITY_PAGE_BYTES sizeof(t_DataLog)
/** @brief Sample records per page. */
#define TAG_IDENTITY_SAMPLES_PER_PAGE (DATALOG_SAMPLES * SAMPLES_PER_BLOCK)
/** @brief Bytes per sample record. */
#define TAG_IDENTITY_SAMPLE_BYTES sizeof(RawSensorData)
/** @brief Nominal sample period in seconds; 0 when it comes from the stored config. */
#define TAG_IDENTITY_SAMPLE_PERIOD_S COMPASS_SAMPLE_PERIOD_S
/** @brief Scale factors, in the order this file documents. */
#define TAG_IDENTITY_SCALES                                                     \
  0.01f, 0.1f, 0.976f, 0.04f, (100.0f / (float)COMPASS_SAMPLE_PERIOD_S)
/** @brief Bytes per calibration slot; sensors.c asserts it. */
#define TAG_IDENTITY_CALIBRATION_SLOT_SIZE 64U
/** @brief Calibration slots in the table; sensors.c asserts it. */
#define TAG_IDENTITY_CALIBRATION_SLOT_COUNT (2048U / 64U)

#endif /* TAG_IDENTITY_FAMILY_H */
