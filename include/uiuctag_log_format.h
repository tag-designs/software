/**
 * @file uiuctag_log_format.h
 * @brief UIUCTag binary log layout shared by firmware and host tools.
 *
 * @details One 8-byte record per minute, stored in external flash:
 *
 *          | Bytes | Field       | Contents                                  |
 *          | ----- | ----------- | ----------------------------------------- |
 *          | 0-1   | activity    | three 5-bit 20 s active-second counts     |
 *          | 2-4   | pressure    | BMP585 PRESS_DATA, unsigned 24-bit LE     |
 *          | 5-7   | temperature | BMP585 TEMP_DATA, signed 24-bit LE        |
 *
 *          Pressure and temperature are the sensor's own data registers,
 *          compensated but unconverted: hPa = raw / 6400, degC = raw / 65536.
 *          The reading is taken at the record's minute boundary, and the
 *          activity word covers the minute that ends there. The whole record is
 *          written at once, as one 8-byte program, at that minute's wake.
 *
 *          Thirty-two records make one 256-byte block, which is one AT25XE
 *          page, anchored by one 8-byte checkpoint in internal flash. The raw
 *          download block t_UIUCTagDataLog is that checkpoint, decoded, plus
 *          the block's bytes exactly as they sit in flash.
 *
 *          Erased flash reads all ones. A written activity word always has
 *          bit 15 clear, so its high byte is never 0xFF, and a record whose
 *          activity word reads 0xFFFF was never written
 *          (uiuctagRecordErased()). A minute the tag did not watch is written
 *          with every bucket UIUCTAG_ACTIVITY_NOT_OBSERVED, word 0x7FFF.
 *
 * @note The binary payload is little-endian.
 *
 * @see embedded/tags/UIUCTag/design/overview.md
 */

#ifndef UIUCTAG_LOG_FORMAT_H
#define UIUCTAG_LOG_FORMAT_H

#include <stdint.h>
#include <string.h>

#ifndef __cplusplus
#include <stdbool.h>
#endif

#if defined(__BYTE_ORDER__) && defined(__ORDER_LITTLE_ENDIAN__)
  #if __BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__
    #error "This binary logging layout only supports little-endian architectures."
  #endif
#elif defined(__BIG_ENDIAN__)
  #error "This binary logging layout only supports little-endian architectures."
#endif

/** @brief Seconds between records: one pressure/temperature reading a minute. */
#define UIUCTAG_RECORD_SECONDS 60u
/** @brief Seconds covered by one activity bucket. */
#define UIUCTAG_ACTIVITY_BUCKET_SECONDS 20u
/** @brief Activity buckets in one record's activity word. */
#define UIUCTAG_ACTIVITY_BUCKETS_PER_RECORD 3u
/** @brief Bits per activity bucket. */
#define UIUCTAG_ACTIVITY_BUCKET_BITS 5u
/** @brief Mask for one activity bucket. */
#define UIUCTAG_ACTIVITY_BUCKET_MASK 0x1fu
/**
 * @brief Reserved bucket value: the bucket was not observed.
 *
 * @details Written in every bucket of a record whose preceding minute the tag
 *          did not watch from end to end: the first record of a run, and the
 *          first after a reset, a missed wake or hibernation. The decoder
 *          skips such a bucket, and any count above
 *          UIUCTAG_ACTIVITY_BUCKET_SECONDS, as unobserved.
 */
#define UIUCTAG_ACTIVITY_NOT_OBSERVED 0x1fu
/** @brief An activity word that was never written (erased flash). */
#define UIUCTAG_ACTIVITY_ERASED 0xffffu
/** @brief An activity word for a minute that was not watched: every bucket
 *         UIUCTAG_ACTIVITY_NOT_OBSERVED, bit 15 clear. */
#define UIUCTAG_ACTIVITY_UNOBSERVED_WORD 0x7fffu
/** @brief Records under one internal checkpoint. */
#define UIUCTAG_LOG_RECORDS 32u
/** @brief Bytes per record. */
#define UIUCTAG_RECORD_SIZE 8u
/** @brief Byte offset of the activity word within a record. */
#define UIUCTAG_RECORD_ACTIVITY_OFFSET 0u
/** @brief Byte offset of the pressure field within a record. */
#define UIUCTAG_RECORD_PRESSURE_OFFSET 2u
/** @brief Byte offset of the temperature field within a record. */
#define UIUCTAG_RECORD_TEMPERATURE_OFFSET 5u
/** @brief Bytes of external flash per block: one AT25XE page. */
#define UIUCTAG_BLOCK_BYTES (UIUCTAG_RECORD_SIZE * UIUCTAG_LOG_RECORDS)
/** @brief Seconds covered by one block. */
#define UIUCTAG_DATA_LOG_SECONDS (UIUCTAG_RECORD_SECONDS * UIUCTAG_LOG_RECORDS)
/** @brief Internal-log checkpoint size in bytes. */
#define UIUCTAG_INTERNAL_LOG_SIZE 8u
/** @brief Complete raw data-log download block size in bytes. */
#define UIUCTAG_DATA_LOG_SIZE (8u + UIUCTAG_BLOCK_BYTES)

/** @brief A 24-bit field that was never written. */
#define UIUCTAG_RAW_ERASED 0xFFFFFFu
/**
 * @brief Stored pressure and temperature of a wake whose reading failed.
 *
 * @details Written for a failed conversion, a DRDY timeout, or a pressure
 *          outside the sensor's range (see UIUCTAG_PRESSURE_RAW_MIN). Zero is
 *          not a possible reading in range, so it cannot be mistaken for one,
 *          and the range check also rejects a bus that read all ones.
 */
#define UIUCTAG_RAW_FAILED 0u
/** @brief Raw pressure counts per hPa (BMP585: 1/64 Pa per count). */
#define UIUCTAG_PRESSURE_COUNTS_PER_HPA 6400.0
/** @brief Raw temperature counts per degree C (BMP585: 2^16). */
#define UIUCTAG_TEMPERATURE_COUNTS_PER_C 65536.0
/** @brief Lowest raw pressure accepted as a reading: 300 hPa. */
#define UIUCTAG_PRESSURE_RAW_MIN (300u * 6400u)
/** @brief Highest raw pressure accepted as a reading: 1250 hPa. */
#define UIUCTAG_PRESSURE_RAW_MAX (1250u * 6400u)

#pragma pack(push, 1)

/**
 * @brief One minute record as stored in external flash.
 *
 * @details Byte arrays rather than integer fields, because the 24-bit values
 *          have no C type; use the helpers below to read them. Activity
 *          bucket b covers [t - 60 + 20b, t - 40 + 20b) for a record at t.
 *          Activity comes first so that the record's first two bytes alone
 *          say whether it was written.
 */
typedef struct {
    uint16_t activity;      /**< Active seconds in the preceding minute: bucket
                                 b (0..2) in bits 5b..5b+4; bit 15 is 0, so a
                                 written word is never UIUCTAG_ACTIVITY_ERASED. */
    uint8_t pressure[3];    /**< Unsigned 24-bit LE pressure, raw / 6400 = hPa. */
    uint8_t temperature[3]; /**< Signed 24-bit LE temperature, raw / 65536 = degC. */
} t_UIUCTagRecord;

/**
 * @brief Internal-flash checkpoint for one external block.
 *
 * @details The epoch is the time of the block's record 0. Collection anchors
 *          its grid at the first minute boundary of the run and each later
 *          block starts UIUCTAG_DATA_LOG_SECONDS after the previous one, so
 *          record times are epoch + slot * UIUCTAG_RECORD_SECONDS with no
 *          normalization. A long gap (hibernation) starts a new grid with a
 *          new checkpoint.
 */
typedef struct {
    int32_t epoch;             /**< Epoch second of record 0 of this block. */
    uint16_t vdd100;           /**< Supply voltage in 0.01 V units. */
    uint16_t extern_log_block; /**< Index of the 256-byte external block. */
} t_UIUCTagInternalLog;

/**
 * @brief Raw download unit for one block.
 *
 * @details The header is the decoded checkpoint; the records are the block's
 *          bytes as stored. Over the monitor protocol the records travel as
 *          UIUCTagLog.samples, trimmed of trailing records that were never
 *          written.
 */
typedef struct {
    int32_t epoch;  /**< Epoch second of record 0. */
    float voltage;  /**< Supply voltage in volts when the block was opened. */
    t_UIUCTagRecord records[UIUCTAG_LOG_RECORDS]; /**< Block, indexed by slot. */
} t_UIUCTagDataLog;

/**
 * @name Layout assertions
 *
 * @details Each typedef fails to compile, with a negative array bound, if a
 *          record stops matching the size the firmware programs and the host
 *          decodes.
 * @{
 */
/** @brief Fails to compile unless one record is UIUCTAG_RECORD_SIZE bytes. */
typedef char uiuctag_record_size_must_be_8[
    sizeof(t_UIUCTagRecord) == UIUCTAG_RECORD_SIZE ? 1 : -1];
/**
 * @brief Fails to compile unless one checkpoint is UIUCTAG_INTERNAL_LOG_SIZE
 *        bytes.
 *
 * @note Internal flash is programmed in 64-bit double words, so this size also
 *       has to stay a multiple of eight.
 */
typedef char uiuctag_internal_log_size_must_be_8[
    sizeof(t_UIUCTagInternalLog) == UIUCTAG_INTERNAL_LOG_SIZE ? 1 : -1];
/** @brief Fails to compile unless a block is exactly one 256-byte page. */
typedef char uiuctag_block_must_be_one_page[
    UIUCTAG_BLOCK_BYTES == 256u ? 1 : -1];
/** @brief Fails to compile unless a download block is UIUCTAG_DATA_LOG_SIZE bytes. */
typedef char uiuctag_data_log_size_must_be_264[
    sizeof(t_UIUCTagDataLog) == UIUCTAG_DATA_LOG_SIZE ? 1 : -1];
/** @brief Fails to compile unless the three buckets fill exactly one minute. */
typedef char uiuctag_buckets_must_fill_a_minute[
    UIUCTAG_ACTIVITY_BUCKET_SECONDS * UIUCTAG_ACTIVITY_BUCKETS_PER_RECORD
        == UIUCTAG_RECORD_SECONDS ? 1 : -1];
/** @} */

#pragma pack(pop)

/**
 * @name Shared encode and decode helpers
 *
 * @details One definition of the record rules for firmware and host, so the
 *          two cannot drift.
 * @{
 */

/**
 * @brief Read a 24-bit little-endian field.
 *
 * @param[in] bytes Three bytes, least significant first.
 * @return The field as an unsigned value, 0 to 0xFFFFFF.
 */
static inline uint32_t uiuctagGet24(const uint8_t bytes[3])
{
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8)
           | ((uint32_t)bytes[2] << 16);
}

/**
 * @brief Write a 24-bit little-endian field.
 *
 * @param[out] bytes Three bytes, least significant first.
 * @param[in] value Value to store; bits above 23 are dropped.
 */
static inline void uiuctagPut24(uint8_t bytes[3], uint32_t value)
{
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8);
    bytes[2] = (uint8_t)(value >> 16);
}

/**
 * @brief Report whether a record was never written.
 *
 * @details Tests the activity word, the record's first two bytes. Every
 *          record the firmware writes has an activity word with bit 15 clear,
 *          so it reads UIUCTAG_ACTIVITY_ERASED only if never written.
 *
 * @param[in] record Record read from flash.
 * @return true when no wake ever wrote this record.
 */
static inline bool uiuctagRecordErased(const t_UIUCTagRecord *record)
{
    return record->activity == UIUCTAG_ACTIVITY_ERASED;
}

/**
 * @brief Raw stored pressure of a record.
 *
 * @param[in] record Record read from flash.
 * @return Unsigned 24-bit pressure field.
 */
static inline uint32_t uiuctagRecordPressureRaw(const t_UIUCTagRecord *record)
{
    return uiuctagGet24(record->pressure);
}

/**
 * @brief Raw stored temperature of a record, sign-extended.
 *
 * @param[in] record Record read from flash.
 * @return Signed 24-bit temperature field as a 32-bit value.
 */
static inline int32_t uiuctagRecordTemperatureRaw(const t_UIUCTagRecord *record)
{
    uint32_t raw = uiuctagGet24(record->temperature);
    if ((raw & 0x800000u) != 0u)
        raw |= 0xFF000000u;
    return (int32_t)raw;
}

/**
 * @brief Report whether a record holds a pressure and temperature reading.
 *
 * @details False for an erased record and for a wake whose reading failed
 *          (UIUCTAG_RAW_FAILED). Pressure and temperature are written together
 *          and judged together.
 *
 * @param[in] record Record read from flash.
 * @return true when the pressure and temperature fields are a reading.
 */
static inline bool uiuctagRecordHasReading(const t_UIUCTagRecord *record)
{
    const uint32_t pressure = uiuctagRecordPressureRaw(record);
    return (pressure != UIUCTAG_RAW_ERASED) && (pressure != UIUCTAG_RAW_FAILED);
}

/**
 * @brief Pressure of a record in hPa.
 *
 * @param[in] record Record read from flash.
 * @return Pressure in hPa. Meaningful only when uiuctagRecordHasReading().
 */
static inline double uiuctagRecordPressureHpa(const t_UIUCTagRecord *record)
{
    return uiuctagRecordPressureRaw(record) / UIUCTAG_PRESSURE_COUNTS_PER_HPA;
}

/**
 * @brief Temperature of a record in degrees C.
 *
 * @param[in] record Record read from flash.
 * @return BMP585 die temperature in degrees C. Meaningful only when
 *         uiuctagRecordHasReading().
 */
static inline double uiuctagRecordTemperatureC(const t_UIUCTagRecord *record)
{
    return uiuctagRecordTemperatureRaw(record)
           / UIUCTAG_TEMPERATURE_COUNTS_PER_C;
}

/**
 * @brief Report whether a record's activity word was written.
 *
 * @details Every record the firmware writes has one; this is false only for
 *          an erased record or a corrupt word.
 *
 * @param[in] record Record read from flash.
 * @return true when bit 15 of the activity word is clear.
 */
static inline bool uiuctagRecordHasActivity(const t_UIUCTagRecord *record)
{
    return (record->activity & 0x8000u) == 0u;
}

/**
 * @brief Active seconds in one bucket of a record.
 *
 * @param[in] record Record read from flash.
 * @param[in] bucket Bucket index, 0 for the first 20 s of the minute before
 *                   the reading.
 * @return Active seconds, 0 to UIUCTAG_ACTIVITY_BUCKET_SECONDS, or
 *         UIUCTAG_ACTIVITY_NOT_OBSERVED when the bucket was not observed, the
 *         value is out of range, @p bucket is out of range, or the record has
 *         no activity word.
 */
static inline uint32_t uiuctagActivityBucket(const t_UIUCTagRecord *record,
                                             unsigned bucket)
{
    uint32_t count;

    if ((bucket >= UIUCTAG_ACTIVITY_BUCKETS_PER_RECORD)
        || !uiuctagRecordHasActivity(record))
        return UIUCTAG_ACTIVITY_NOT_OBSERVED;

    count = ((uint32_t)record->activity >> (bucket * UIUCTAG_ACTIVITY_BUCKET_BITS))
            & UIUCTAG_ACTIVITY_BUCKET_MASK;
    return (count > UIUCTAG_ACTIVITY_BUCKET_SECONDS)
               ? UIUCTAG_ACTIVITY_NOT_OBSERVED : count;
}

/**
 * @brief Epoch time of one record of a block.
 *
 * @param[in] header_epoch Epoch seconds of the block's record 0.
 * @param[in] slot Record index within the block, 0 to UIUCTAG_LOG_RECORDS-1.
 * @return Epoch seconds at which the record's reading was taken, which is
 *         also when its activity minute ends.
 */
static inline int32_t uiuctagRecordEpoch(int32_t header_epoch, unsigned slot)
{
    return header_epoch + (int32_t)(slot * UIUCTAG_RECORD_SECONDS);
}

/**
 * @brief Epoch time at which one activity bucket starts.
 *
 * @param[in] header_epoch Epoch seconds of the block's record 0.
 * @param[in] slot Record index within the block.
 * @param[in] bucket Bucket index within the record.
 * @return Epoch seconds at the start of that 20 s bucket, in the minute
 *         before the record's reading.
 */
static inline int32_t uiuctagActivityBucketEpoch(int32_t header_epoch,
                                                 unsigned slot,
                                                 unsigned bucket)
{
    return uiuctagRecordEpoch(header_epoch, slot)
           - (int32_t)UIUCTAG_RECORD_SECONDS
           + (int32_t)(bucket * UIUCTAG_ACTIVITY_BUCKET_SECONDS);
}
/** @} */

#endif
