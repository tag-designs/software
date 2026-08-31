/**
 * @file uiuctag_log_format.h
 * @brief UIUCTag binary log layout shared by firmware and host tools.
 *
 * @details Defines the compact pressure/activity sample records stored in
 *          external flash and the raw download block that wraps those samples
 *          with internal-log metadata.
 *
 * @note The binary payload is little-endian and stores floats in the target C
 *       float representation used by the firmware build.
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

/** @brief Number of seconds covered by one packed activity bucket. */
#define UIUCTAG_ACTIVITY_BUCKET_SECONDS 60u
/** @brief Number of six-bit activity buckets packed into one external block. */
#define UIUCTAG_ACTIVITY_BUCKETS_PER_EXTERNAL_BLOCK 5u
/** @brief Number of seconds covered by one external pressure/activity block. */
#define UIUCTAG_EXTERNAL_BLOCK_SECONDS \
    (UIUCTAG_ACTIVITY_BUCKET_SECONDS * UIUCTAG_ACTIVITY_BUCKETS_PER_EXTERNAL_BLOCK)
/** @brief Number of five-minute samples carried by one raw download block. */
#define UIUCTAG_LOG_SAMPLES 24u
/** @brief Number of seconds covered by one raw download block. */
#define UIUCTAG_DATA_LOG_SECONDS \
    (UIUCTAG_EXTERNAL_BLOCK_SECONDS * UIUCTAG_LOG_SAMPLES)
/** @brief Mask for one six-bit activity bucket inside packed_activity_data. */
#define UIUCTAG_ACTIVITY_BUCKET_MASK 0x3fu
/** @brief Number of bits per packed activity bucket. */
#define UIUCTAG_ACTIVITY_BUCKET_BITS 6u
/**
 * @def   UIUCTAG_ERASED_WORD
 * @brief Value read back from an external-flash word that was never written.
 *
 * @details Erased NOR flash reads as all ones. The firmware never writes this
 *          value deliberately, so it marks a sample slot, or a field within a
 *          slot, that no wake ever filled in.
 */
#define UIUCTAG_ERASED_WORD 0xFFFFFFFFu
/** @brief Five-minute sample size in bytes. */
#define UIUCTAG_SAMPLE_SIZE 12u
/** @brief Internal-log checkpoint size in bytes. */
#define UIUCTAG_INTERNAL_LOG_SIZE 8u
/** @brief Maximum protobuf bytes payload for one raw sample group. */
#define UIUCTAG_SAMPLE_BYTES_MAX (UIUCTAG_SAMPLE_SIZE * UIUCTAG_LOG_SAMPLES)
/** @brief Complete raw data-log download block size in bytes. */
#define UIUCTAG_DATA_LOG_SIZE (8u + (UIUCTAG_SAMPLE_SIZE * UIUCTAG_LOG_SAMPLES))

#pragma pack(push, 1)

/**
 * @brief Five-minute pressure and activity sample stored in external flash.
 *
 * @details Pressure is stored in hPa and temperature is the BMP585 die
 *          temperature in degrees C. The low 30 bits of packed_activity_data
 *          hold five one-minute buckets, with bucket 0 in bits 0..5 and bucket
 *          4 in bits 24..29.
 */
typedef struct {
    float pressure;    /**< Pressure in hPa, or NaN when not measured. */
    float temperature; /**< Die temperature in degrees C, or NaN when not
                            measured. */
    uint32_t packed_activity_data; /**< Five six-bit active-second counts, or
                                        UIUCTAG_ERASED_WORD when not yet
                                        written. */
} t_UIUCTagSample;

/**
 * @brief Internal-flash checkpoint for a raw UIUCTag sample group.
 *
 * @details The external log block identifies the first 12-byte sample in the
 *          external log region associated with this checkpoint. Subsequent
 *          samples can be derived from the fixed sample size and count.
 *
 *          The epoch is the time of this block's slot 0, not a rounded window
 *          boundary: collection anchors its sample grid at the first minute
 *          boundary of the run, and each later block starts exactly
 *          UIUCTAG_DATA_LOG_SECONDS after the previous one. Sample times are
 *          therefore epoch + slot * UIUCTAG_EXTERNAL_BLOCK_SECONDS with no
 *          normalization, and two blocks can never share a start time.
 */
typedef struct {
    int32_t epoch;           /**< Epoch second of slot 0 of this block. */
    uint16_t vdd100;         /**< Supply voltage in 0.01 V units. */
    uint16_t extern_log_block; /**< Index of the 288-byte external block. */
} t_UIUCTagInternalLog;

/**
 * @brief Compact raw host download unit for one UIUCTag sample group.
 *
 * @details Header fields are decoded from the matching internal checkpoint.
 *          The sample array is the raw external-flash payload that maps to
 *          UIUCTagLog.samples in the protobuf response.
 */
typedef struct {
    int32_t epoch;  /**< Raw wake time the block was opened, epoch seconds. */
    float voltage;  /**< Supply voltage in volts at that moment. */
    t_UIUCTagSample samples[UIUCTAG_LOG_SAMPLES]; /**< Block payload, indexed by
                                                       slot. */
} t_UIUCTagDataLog;

/**
 * @name Layout assertions
 *
 * @details These typedefs fail to compile if a record ever stops matching the
 *          size the firmware programs and the host decodes, which is the one
 *          error in this file that cannot be caught by inspection. Each is
 *          declared with a negative array bound when its size check fails.
 * @{
 */
/** @brief Fails to compile unless one sample is UIUCTAG_SAMPLE_SIZE bytes. */
typedef char uiuctag_sample_size_must_be_12[
    sizeof(t_UIUCTagSample) == UIUCTAG_SAMPLE_SIZE ? 1 : -1];
/**
 * @brief Fails to compile unless one checkpoint is
 *        UIUCTAG_INTERNAL_LOG_SIZE bytes.
 *
 * @note The checkpoint is programmed into internal flash, whose write
 *       granularity is a 64-bit double word, so this size also has to stay a
 *       multiple of eight.
 */
typedef char uiuctag_internal_log_size_must_be_8[
    sizeof(t_UIUCTagInternalLog) == UIUCTAG_INTERNAL_LOG_SIZE ? 1 : -1];
/**
 * @brief Fails to compile unless one download block is UIUCTAG_DATA_LOG_SIZE
 *        bytes.
 */
typedef char uiuctag_data_log_size_must_be_296[
    sizeof(t_UIUCTagDataLog) == UIUCTAG_DATA_LOG_SIZE ? 1 : -1];
/** @} */

#pragma pack(pop)

/**
 * @name Shared decode helpers
 *
 * @details One definition of the UIUCTag decode rules for both firmware and
 *          host tools. Keeping them here is what stops the two sides from
 *          drifting: the record geometry, the missing-value rule, and the
 *          slot-to-epoch mapping each exist exactly once.
 * @{
 */

/**
 * @brief Reinterpret a float as its IEEE-754 bit pattern.
 *
 * @param[in] value Value to inspect.
 * @return Raw 32-bit encoding of @p value.
 *
 * @note Uses memcpy rather than a pointer cast so the read stays defined under
 *       strict aliasing in both C and C++.
 */
static inline uint32_t uiuctagFloatBits(float value)
{
    uint32_t bits;
    memcpy(&bits, &value, sizeof(bits));
    return bits;
}

/**
 * @brief Report whether a stored float carries no measurement.
 *
 * @details Tests for any NaN — exponent all ones with a nonzero mantissa —
 *          rather than one specific encoding, because two different NaNs mean
 *          "missing" in this log. Erased flash reads as UIUCTAG_ERASED_WORD,
 *          which is a quiet NaN with all mantissa bits set, and a failed sensor
 *          conversion stores the compiler's canonical quiet NaN. Both are
 *          absent data; the distinction is only whether the tag ever woke for
 *          that slot.
 *
 * @param[in] value Stored pressure or temperature value.
 * @return true when @p value is any NaN and therefore not a measurement.
 */
static inline bool uiuctagFloatMissing(float value)
{
    const uint32_t bits = uiuctagFloatBits(value);
    return ((bits & 0x7f800000u) == 0x7f800000u)
        && ((bits & 0x007fffffu) != 0u);
}

/**
 * @brief Report whether a sample slot holds a pressure measurement.
 *
 * @param[in] sample Sample decoded from the external log.
 * @return true when the pressure field is a real measurement.
 */
static inline bool uiuctagSampleHasPressure(const t_UIUCTagSample *sample)
{
    return !uiuctagFloatMissing(sample->pressure);
}

/**
 * @brief Report whether a sample slot holds a temperature measurement.
 *
 * @param[in] sample Sample decoded from the external log.
 * @return true when the temperature field is a real measurement.
 */
static inline bool uiuctagSampleHasTemperature(const t_UIUCTagSample *sample)
{
    return !uiuctagFloatMissing(sample->temperature);
}

/**
 * @brief Report whether a sample slot holds packed activity counts.
 *
 * @details The activity word is written one sample period after the pressure
 *          fields of the same slot, so a slot with valid pressure and an erased
 *          activity word is the normal state of the most recent sample, not a
 *          fault.
 *
 * @param[in] sample Sample decoded from the external log.
 * @return true when the activity word was written.
 */
static inline bool uiuctagSampleHasActivity(const t_UIUCTagSample *sample)
{
    return sample->packed_activity_data != UIUCTAG_ERASED_WORD;
}

/**
 * @brief Report whether a sample slot was never written at all.
 *
 * @param[in] sample Sample decoded from the external log.
 * @return true when no field of the slot holds data.
 */
static inline bool uiuctagSampleErased(const t_UIUCTagSample *sample)
{
    return !uiuctagSampleHasPressure(sample)
        && !uiuctagSampleHasTemperature(sample)
        && !uiuctagSampleHasActivity(sample);
}

/**
 * @brief Unpack one six-bit activity bucket from a sample.
 *
 * @param[in] sample Sample decoded from the external log.
 * @param[in] bucket Bucket index, 0 for the first minute of the sample period.
 * @return Count of active seconds in that minute, 0 to 60. Returns 0 for a
 *         bucket index at or beyond
 *         UIUCTAG_ACTIVITY_BUCKETS_PER_EXTERNAL_BLOCK.
 *
 * @warning Only meaningful when uiuctagSampleHasActivity() is true; an erased
 *          word would otherwise decode as a saturated count in every bucket.
 */
static inline uint32_t uiuctagActivityBucket(const t_UIUCTagSample *sample,
                                             unsigned bucket)
{
    if (bucket >= UIUCTAG_ACTIVITY_BUCKETS_PER_EXTERNAL_BLOCK)
        return 0u;

    return (sample->packed_activity_data
            >> (bucket * UIUCTAG_ACTIVITY_BUCKET_BITS))
           & UIUCTAG_ACTIVITY_BUCKET_MASK;
}

/**
 * @brief Epoch time of one sample slot within a block.
 *
 * @details The checkpoint epoch is the time of the block's own slot 0, so slot
 *          times are a plain offset from it. Collection anchors the grid at the
 *          first minute boundary of the run rather than at an absolute
 *          multiple of the block period, which is why no rounding is involved:
 *          a block's samples always begin at slot 0, and a block that ends
 *          early simply has unwritten slots at the end.
 *
 * @param[in] header_epoch Epoch seconds from the internal checkpoint.
 * @param[in] slot Slot index within the block, 0 to UIUCTAG_LOG_SAMPLES-1.
 * @return Epoch seconds at which that slot's pressure was measured, and at
 *         which its first activity bucket starts.
 */
static inline int32_t uiuctagSampleEpoch(int32_t header_epoch, unsigned slot)
{
    return header_epoch
           + (int32_t)(slot * UIUCTAG_EXTERNAL_BLOCK_SECONDS);
}

/**
 * @brief Epoch time at which one activity bucket of a slot starts.
 *
 * @param[in] header_epoch Raw epoch seconds from the internal checkpoint.
 * @param[in] slot Slot index within the block.
 * @param[in] bucket Bucket index within the slot.
 * @return Epoch seconds at the start of that one-minute bucket.
 */
static inline int32_t uiuctagActivityBucketEpoch(int32_t header_epoch,
                                                 unsigned slot,
                                                 unsigned bucket)
{
    return uiuctagSampleEpoch(header_epoch, slot)
           + (int32_t)(bucket * UIUCTAG_ACTIVITY_BUCKET_SECONDS);
}
/** @} */

#endif
