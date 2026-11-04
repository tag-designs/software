/**
 * @file    uiuctag_format_check.cc
 * @brief   Assertion check for the shared UIUCTag log-format helpers.
 *
 * @details include/uiuctag_log_format.h is the single definition of the UIUCTag
 *          record layout and decode rules for both firmware and host, so a
 *          mistake in it is a mistake on both sides at once and cannot be
 *          caught by cross-checking them against each other. This exercises the
 *          cases that matter: field order, the erased-record test on the
 *          activity word and why no written record can pass it, the failed
 *          reading, 24-bit sign extension and the raw-to-unit conversions,
 *          activity bucket order with its 0, 20 and not-observed values, and
 *          record and bucket epochs for a block anchored mid-hour.
 *
 *          It also confirms the header compiles as C++; the firmware
 *          simulations include the same header as C11.
 *
 * @note    Built only when BUILD_TAGCORE_CHECKS is enabled. See README.md.
 *
 * @see     embedded/tags/UIUCTag/design/test-strategy.md
 */

// These are assertion programs: keep assert() live in Release builds too,
// where CMake defines NDEBUG and would otherwise compile every check out.
#undef NDEBUG

#include "uiuctag_log_format.h"
#include <cassert>
#include <cstddef>
#include <cmath>
#include <cstdio>
#include <cstring>

/**
 * @brief Build a record from field values.
 *
 * @param[in] pressure Stored 24-bit pressure.
 * @param[in] temperature Stored 24-bit temperature (two's complement).
 * @param[in] activity Activity word.
 * @return The record as the firmware would program it.
 */
static t_UIUCTagRecord makeRecord(uint32_t pressure, uint32_t temperature,
                                  uint16_t activity)
{
    t_UIUCTagRecord r;
    uiuctagPut24(r.pressure, pressure);
    uiuctagPut24(r.temperature, temperature);
    r.activity = activity;
    return r;
}

/**
 * @brief Assert the shared format helpers behave as both sides assume.
 *
 * @return 0 when every assertion holds; aborts on the first that does not.
 */
int main()
{
    // Sizes and geometry.
    assert(sizeof(t_UIUCTagRecord) == 8);
    assert(sizeof(t_UIUCTagInternalLog) == 8);
    assert(sizeof(t_UIUCTagDataLog) == 264);
    assert(UIUCTAG_BLOCK_BYTES == 256);
    assert(UIUCTAG_DATA_LOG_SECONDS == 1920);
    assert(offsetof(t_UIUCTagRecord, temperature) == UIUCTAG_RECORD_TEMPERATURE_OFFSET);
    assert(offsetof(t_UIUCTagRecord, activity) == UIUCTAG_RECORD_ACTIVITY_OFFSET);
    // 4 MiB of AT25XE321D holds 364 days of minute records.
    assert((4194304u / UIUCTAG_RECORD_SIZE) / 1440u == 364u);

    // Field byte order: activity, pressure, temperature, little endian.
    t_UIUCTagRecord r = makeRecord(0x123456, 0x789ABC, 0x1234);
    const uint8_t *bytes = reinterpret_cast<const uint8_t *>(&r);
    const uint8_t expect[8] = {0x34, 0x12, 0x56, 0x34, 0x12, 0xBC, 0x9A, 0x78};
    assert(std::memcmp(bytes, expect, 8) == 0);

    // Erased record: activity word all ones.
    t_UIUCTagRecord erased;
    std::memset(&erased, 0xFF, sizeof(erased));
    assert(uiuctagRecordErased(&erased));
    assert(!uiuctagRecordHasReading(&erased));
    assert(!uiuctagRecordHasActivity(&erased));
    assert(uiuctagActivityBucket(&erased, 0) == UIUCTAG_ACTIVITY_NOT_OBSERVED);

    // A written activity word has bit 15 clear, so a written record is never
    // erased whatever its other bytes hold -- the unobserved word included.
    r = makeRecord(0xFFFFFF, 0xFFFFFF, UIUCTAG_ACTIVITY_UNOBSERVED_WORD);
    assert(!uiuctagRecordErased(&r));

    // Failed reading: written record, no reading, activity still valid.
    r = makeRecord(UIUCTAG_RAW_FAILED, UIUCTAG_RAW_FAILED, 5);
    assert(!uiuctagRecordErased(&r));
    assert(!uiuctagRecordHasReading(&r));
    assert(uiuctagActivityBucket(&r, 0) == 5);

    // Conversions, including negative temperature.
    r = makeRecord(101325u * 64u, (uint32_t)(-10 * 65536) & 0xFFFFFFu, 0);
    assert(uiuctagRecordHasReading(&r));
    assert(std::fabs(uiuctagRecordPressureHpa(&r) - 1013.25) < 1e-9);
    assert(uiuctagRecordTemperatureRaw(&r) == -10 * 65536);
    assert(std::fabs(uiuctagRecordTemperatureC(&r) + 10.0) < 1e-12);
    r = makeRecord(1000 * 6400, 0x7FFFFF, 0);
    assert(uiuctagRecordTemperatureRaw(&r) == 0x7FFFFF);
    r = makeRecord(1000 * 6400, 0x800000, 0);
    assert(uiuctagRecordTemperatureRaw(&r) == -0x800000);

    // Activity: bucket 0 in the low bits, 0 and 20 at the edges.
    r = makeRecord(1000 * 6400, 0, (uint16_t)(0u | (20u << 5) | (7u << 10)));
    assert(uiuctagRecordHasActivity(&r));
    assert(uiuctagActivityBucket(&r, 0) == 0);
    assert(uiuctagActivityBucket(&r, 1) == 20);
    assert(uiuctagActivityBucket(&r, 2) == 7);
    assert(uiuctagActivityBucket(&r, 3) == UIUCTAG_ACTIVITY_NOT_OBSERVED);
    // A minute that was not watched.
    r.activity = UIUCTAG_ACTIVITY_UNOBSERVED_WORD;
    assert(uiuctagRecordHasActivity(&r));
    for (unsigned b = 0; b < 3; b++)
        assert(uiuctagActivityBucket(&r, b) == UIUCTAG_ACTIVITY_NOT_OBSERVED);
    // Counts 21..30 cannot occur and read as not observed, never above 100%.
    r.activity = 21u;
    assert(uiuctagActivityBucket(&r, 0) == UIUCTAG_ACTIVITY_NOT_OBSERVED);
    // Bit 15 set is not a written word.
    r.activity = 0x8000u;
    assert(!uiuctagRecordHasActivity(&r));

    // Timing. The checkpoint epoch is the block's own record 0; activity
    // covers the minute before each record.
    const int32_t anchor = 1767225780;   // a minute boundary, not a block one
    assert(anchor % 60 == 0 && anchor % 1920 != 0);
    assert(uiuctagRecordEpoch(anchor, 0) == anchor);
    assert(uiuctagRecordEpoch(anchor, 31) == anchor + 31 * 60);
    assert(uiuctagActivityBucketEpoch(anchor, 0, 0) == anchor - 60);
    assert(uiuctagActivityBucketEpoch(anchor, 1, 2) == anchor + 40);
    // The last bucket of a record ends exactly at the record's reading.
    assert(uiuctagActivityBucketEpoch(anchor, 5, 2) + 20
           == uiuctagRecordEpoch(anchor, 5));

    printf("uiuctag_format_check: all assertions passed\n");
    return 0;
}
