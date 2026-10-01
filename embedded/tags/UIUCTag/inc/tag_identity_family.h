/**
 * @file    tag_identity_family.h
 * @brief   UIUCTag facts for the tag identity record.
 *
 * @details Replaces common/core/inc/tag_identity_family.h by basename; see
 *          that file for the macros. Each 8-byte internal checkpoint carries
 *          the index of its external block; a block is UIUCTAG_LOG_SAMPLES
 *          12-byte {float pressure, float temperature, uint32 activity} slots
 *          (uiuctag_log_format.h), one slot per UIUCTAG_EXTERNAL_BLOCK_SECONDS.
 *          Pressure and temperature are stored in hPa and degC, unscaled.
 *
 *          Scale slots:
 *          0. checkpoint voltage: vdd100 * 0.01 (V).
 */

#ifndef TAG_IDENTITY_FAMILY_H
#define TAG_IDENTITY_FAMILY_H

/** @brief Firmware family name. */
#define TAG_IDENTITY_FAMILY "BitPresTag"
/** @brief Host sqlitelog decoder that reads this family's data log. */
#define TAG_IDENTITY_DECODER "uiuctag"
/** @brief Version of the data layout; bump when it changes. */
#define TAG_IDENTITY_DATA_LAYOUT_VERSION 1U
/** @brief How a header finds its external data (TAG_IDENTITY_MAP_*). */
#define TAG_IDENTITY_MAPPING TAG_IDENTITY_MAP_BLOCK_FIELD
/** @brief Bytes per internal header. */
#define TAG_IDENTITY_DATA_HEADER_SIZE sizeof(t_DataHeader)
/** @brief Bytes of external data per header or page. */
#define TAG_IDENTITY_PAGE_BYTES DATALOG_BLOCK_BYTES
/** @brief Sample records per page. */
#define TAG_IDENTITY_SAMPLES_PER_PAGE UIUCTAG_LOG_SAMPLES
/** @brief Bytes per sample record. */
#define TAG_IDENTITY_SAMPLE_BYTES UIUCTAG_SAMPLE_SIZE
/** @brief Nominal sample period in seconds; 0 when it comes from the stored config. */
#define TAG_IDENTITY_SAMPLE_PERIOD_S UIUCTAG_EXTERNAL_BLOCK_SECONDS
/** @brief Scale factors, in the order this file documents. */
#define TAG_IDENTITY_SCALES 0.01f

#endif /* TAG_IDENTITY_FAMILY_H */
