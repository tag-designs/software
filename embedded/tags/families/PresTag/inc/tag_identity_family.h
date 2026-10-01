/**
 * @file    tag_identity_family.h
 * @brief   PresTag family facts for the tag identity record.
 *
 * @details Replaces common/core/inc/tag_identity_family.h by basename; see
 *          that file for the macros. Header i owns external bytes
 *          [i*240, (i+1)*240): DATALOG_SAMPLES packed {int16 pressure, int16
 *          temperature} samples (prestag_log_format.h). The sample period
 *          comes from the stored configuration (lps_period).
 *
 *          Scale slots, in this order, as data_logAck() applies them:
 *          0. header voltage: vdd100[0] * 0.01;
 *          1. sample pressure: raw / 16 (hPa) -- a divisor;
 *          2. sample temperature: raw / 100 (degC) -- a divisor;
 *          3. header second voltage: vdd100[1] * 0.01.
 */

#ifndef TAG_IDENTITY_FAMILY_H
#define TAG_IDENTITY_FAMILY_H

/** @brief Firmware family name. */
#define TAG_IDENTITY_FAMILY "PresTag"
#if defined(PRESTAG_RAW_LOG) && PRESTAG_RAW_LOG
/** @brief Host sqlitelog decoder that reads this family's data log. */
#define TAG_IDENTITY_DECODER "prestag_raw"
#else
/** @brief Host sqlitelog decoder that reads this family's data log. */
#define TAG_IDENTITY_DECODER "prestag"
#endif
/** @brief Version of the data layout; bump when it changes. */
#define TAG_IDENTITY_DATA_LAYOUT_VERSION 1U
/** @brief How a header finds its external data (TAG_IDENTITY_MAP_*). */
#define TAG_IDENTITY_MAPPING TAG_IDENTITY_MAP_STRIDE
/** @brief Bytes per internal header. */
#define TAG_IDENTITY_DATA_HEADER_SIZE sizeof(t_DataHeader)
/** @brief Bytes of external data per header or page. */
#define TAG_IDENTITY_PAGE_BYTES sizeof(t_DataLog)
/** @brief Sample records per page. */
#define TAG_IDENTITY_SAMPLES_PER_PAGE DATALOG_SAMPLES
/** @brief Bytes per sample record. */
#define TAG_IDENTITY_SAMPLE_BYTES (sizeof(t_DataLog) / DATALOG_SAMPLES)
/** @brief Nominal sample period in seconds; 0 when it comes from the stored config. */
#define TAG_IDENTITY_SAMPLE_PERIOD_S 0U
/** @brief Scale factors, in the order this file documents. */
#define TAG_IDENTITY_SCALES 0.01f, 16.0f, 100.0f, 0.01f

#endif /* TAG_IDENTITY_FAMILY_H */
