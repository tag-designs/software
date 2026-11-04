/**
 * @file    tag_identity_family.h
 * @brief   UIUCTag facts for the tag identity record.
 *
 * @details Replaces common/core/inc/tag_identity_family.h by basename; see
 *          that file for the macros. Each 8-byte internal checkpoint carries
 *          the index of its external block; a block is UIUCTAG_LOG_RECORDS
 *          8-byte {uint24 pressure, int24 temperature, uint16 activity}
 *          records (uiuctag_log_format.h), one per UIUCTAG_RECORD_SECONDS.
 *          Pressure and temperature are the BMP585's raw registers:
 *          hPa = raw / 6400, degC = raw / 65536. Layout 1 was the 12-byte,
 *          five-minute float format, retired in fw 2.0.0.
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
#define TAG_IDENTITY_DATA_LAYOUT_VERSION 2U
/** @brief How a header finds its external data (TAG_IDENTITY_MAP_*). */
#define TAG_IDENTITY_MAPPING TAG_IDENTITY_MAP_BLOCK_FIELD
/** @brief Bytes per internal header. */
#define TAG_IDENTITY_DATA_HEADER_SIZE sizeof(t_DataHeader)
/** @brief Bytes of external data per header or page. */
#define TAG_IDENTITY_PAGE_BYTES DATALOG_BLOCK_BYTES
/** @brief Sample records per page. */
#define TAG_IDENTITY_SAMPLES_PER_PAGE UIUCTAG_LOG_RECORDS
/** @brief Bytes per sample record. */
#define TAG_IDENTITY_SAMPLE_BYTES UIUCTAG_RECORD_SIZE
/** @brief Nominal sample period in seconds; 0 when it comes from the stored config. */
#define TAG_IDENTITY_SAMPLE_PERIOD_S UIUCTAG_RECORD_SECONDS
/** @brief Scale factors, in the order this file documents. */
#define TAG_IDENTITY_SCALES 0.01f

#endif /* TAG_IDENTITY_FAMILY_H */
