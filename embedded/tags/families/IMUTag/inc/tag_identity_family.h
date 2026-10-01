/**
 * @file    tag_identity_family.h
 * @brief   IMUTag family facts for the tag identity record.
 *
 * @details Replaces common/core/inc/tag_identity_family.h by basename; see
 *          that file for the macros. Internal headers are checkpoints carrying
 *          logical and physical page cursors (IMUTAG_CHECKPOINT_PAGES pages
 *          each); external pages are IMUTAG_PAGE_SIZE bytes of
 *          imutag_log_format.h superframes on SPI NAND. The sample rate and
 *          ranges come from the stored configuration; header times use 1/1024 s
 *          ticks.
 *
 *          Scale slots, in this order:
 *          0. page-header temperature: rawtemp * 0.01 (degC);
 *          1. aux pressure: IMUTAG_PRESSURE_HPA_PER_LSB;
 *          2. aux magnetic field: IMUTAG_MAG_UT_PER_LSB.
 *
 *          Calibration slots are {int32 timestamp; 13 floats}, padded to 64
 *          bytes on the STM32U375 (56 elsewhere), in one 2048-byte table;
 *          sensors.c asserts the size.
 */

#ifndef TAG_IDENTITY_FAMILY_H
#define TAG_IDENTITY_FAMILY_H

#include "imutag_log_format.h"

/** @brief Firmware family name. */
#define TAG_IDENTITY_FAMILY "IMUTag"
/** @brief Host sqlitelog decoder that reads this family's data log. */
#define TAG_IDENTITY_DECODER "imutag"
/** @brief Version of the data layout; bump when it changes. */
#define TAG_IDENTITY_DATA_LAYOUT_VERSION 1U
/** @brief How a header finds its external data (TAG_IDENTITY_MAP_*). */
#define TAG_IDENTITY_MAPPING TAG_IDENTITY_MAP_CHECKPOINT
/** @brief Bytes per internal header. */
#define TAG_IDENTITY_DATA_HEADER_SIZE sizeof(t_InternalDataHeader)
/** @brief Bytes of external data per header or page. */
#define TAG_IDENTITY_PAGE_BYTES IMUTAG_PAGE_SIZE
/** @brief Sample records per page. */
#define TAG_IDENTITY_SAMPLES_PER_PAGE IMUTAG_IMU_SAMPLES_PER_PAGE
/** @brief Bytes per sample record. */
#define TAG_IDENTITY_SAMPLE_BYTES sizeof(t_ImuTagImuSample)
/** @brief Nominal sample period in seconds; 0 when it comes from the stored config. */
#define TAG_IDENTITY_SAMPLE_PERIOD_S 0U
/** @brief Sub-second tick rate of header timestamps; 0 if none. */
#define TAG_IDENTITY_SUBSECOND_HZ 1024U
/** @brief Scale factors, in the order this file documents. */
#define TAG_IDENTITY_SCALES                                                     \
  0.01f, (float)IMUTAG_PRESSURE_HPA_PER_LSB, (float)IMUTAG_MAG_UT_PER_LSB
#if defined(TAG_STM32U3_FLASH) && TAG_STM32U3_FLASH
/** @brief Bytes per calibration slot; sensors.c asserts it. */
#define TAG_IDENTITY_CALIBRATION_SLOT_SIZE 64U
#else
/** @brief Bytes per calibration slot; sensors.c asserts it. */
#define TAG_IDENTITY_CALIBRATION_SLOT_SIZE 56U
#endif
/** @brief Calibration slots in the table; sensors.c asserts it. */
#define TAG_IDENTITY_CALIBRATION_SLOT_COUNT                                     \
  (2048U / TAG_IDENTITY_CALIBRATION_SLOT_SIZE)

#endif /* TAG_IDENTITY_FAMILY_H */
