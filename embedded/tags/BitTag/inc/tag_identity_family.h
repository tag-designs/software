/**
 * @file    tag_identity_family.h
 * @brief   BitTag facts for the tag identity record.
 *
 * @details Replaces common/core/inc/tag_identity_family.h by basename; see
 *          that file for the macros. BitTag has no external flash: each
 *          16-byte internal record {int32 epoch; int16 temp10; uint16 vdd100;
 *          uint64 activity} is the whole log. The activity format and period
 *          come from the stored configuration.
 *
 *          Scale slots, in this order, as data_logAck() applies them:
 *          0. temperature: temp10 * 0.1 (degC);
 *          1. voltage: vdd100 * 0.01 (V).
 */

#ifndef TAG_IDENTITY_FAMILY_H
#define TAG_IDENTITY_FAMILY_H

/** @brief Firmware family name. */
#define TAG_IDENTITY_FAMILY "BitTag"
/** @brief Host sqlitelog decoder that reads this family's data log. */
#define TAG_IDENTITY_DECODER "bittag"
/** @brief Version of the data layout; bump when it changes. */
#define TAG_IDENTITY_DATA_LAYOUT_VERSION 1U
/** @brief How a header finds its external data (TAG_IDENTITY_MAP_*). */
#define TAG_IDENTITY_MAPPING TAG_IDENTITY_MAP_NONE
/** @brief Bytes per internal header. */
#define TAG_IDENTITY_DATA_HEADER_SIZE sizeof(t_DataHeader)
/** @brief Nominal sample period in seconds; 0 when it comes from the stored config. */
#define TAG_IDENTITY_SAMPLE_PERIOD_S 0U
/** @brief Scale factors, in the order this file documents. */
#define TAG_IDENTITY_SCALES 0.1f, 0.01f

#endif /* TAG_IDENTITY_FAMILY_H */
