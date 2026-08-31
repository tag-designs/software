/**
 * @file    uiuctag_format_check.cc
 * @brief   Assertion check for the shared UIUCTag log-format helpers.
 *
 * @details include/uiuctag_log_format.h is the single definition of the UIUCTag
 *          record layout and decode rules for both firmware and host, so a
 *          mistake in it is a mistake on both sides at once and cannot be
 *          caught by cross-checking them against each other. This exercises the
 *          cases that matter: the two different NaN encodings that both mean
 *          "missing", values that must not be mistaken for missing, activity
 *          bucket order and its 0 and 60 edges, and slot-to-epoch mapping for a
 *          block opened mid-window.
 *
 *          It also confirms the header compiles as C++; uiuctag_format_check
 *          has a C counterpart in the firmware simulation, which includes the
 *          same header as C11.
 *
 * @note    Built only when BUILD_TAGCORE_CHECKS is enabled. See README.md.
 *
 * @see     embedded/tags/families/BitPresTag/design/uiuctag-test-strategy.md
 */

#include "uiuctag_log_format.h"
#include <cstdio>
#include <cstring>
#include <cmath>
#include <cassert>

/**
 * @brief Assert the shared format helpers behave as both sides assume.
 *
 * @return 0 when every assertion holds; aborts on the first that does not.
 */
int main()
{
    // sizes
    assert(sizeof(t_UIUCTagSample) == 12);
    assert(sizeof(t_UIUCTagInternalLog) == 8);
    assert(sizeof(t_UIUCTagDataLog) == 296);
    assert(UIUCTAG_SAMPLE_BYTES_MAX == 288);

    // erased slot: every field all-ones
    t_UIUCTagSample erased;
    memset(&erased, 0xFF, sizeof(erased));
    assert(!uiuctagSampleHasPressure(&erased));
    assert(!uiuctagSampleHasTemperature(&erased));
    assert(!uiuctagSampleHasActivity(&erased));
    assert(uiuctagSampleErased(&erased));

    // canonical quiet NaN from a failed conversion must also read as missing
    t_UIUCTagSample failed;
    memset(&failed, 0xFF, sizeof(failed));
    failed.pressure = __builtin_nanf("");
    failed.temperature = __builtin_nanf("");
    assert(!uiuctagSampleHasPressure(&failed));
    assert(!uiuctagSampleHasTemperature(&failed));
    printf("canonical NaN bits = 0x%08x, erased bits = 0x%08x\n",
           uiuctagFloatBits(failed.pressure), UIUCTAG_ERASED_WORD);

    // real values, including edge cases, must NOT read as missing
    t_UIUCTagSample real;
    memset(&real, 0, sizeof(real));
    const float values[] = {1013.25f, 0.0f, -40.0f, 1e-38f, 3.4e38f};
    for (float v : values) {
        real.pressure = v;
        assert(uiuctagSampleHasPressure(&real));
    }
    real.pressure = INFINITY;   // infinity is not NaN: not "missing"
    assert(uiuctagSampleHasPressure(&real));

    // activity: buckets 0..4, bucket 0 in the low bits, 0..60 counts
    real.packed_activity_data = 0;
    for (unsigned b = 0; b < 5; b++) {
        real.packed_activity_data |= (uint32_t)((b + 1) * 10) << (b * 6);
    }
    assert(uiuctagSampleHasActivity(&real));
    for (unsigned b = 0; b < 5; b++) {
        assert(uiuctagActivityBucket(&real, b) == (b + 1) * 10);
    }
    assert(uiuctagActivityBucket(&real, 5) == 0);   // out of range
    real.packed_activity_data = 60u | (60u << 24);  // 60 fits 6 bits
    assert(uiuctagActivityBucket(&real, 0) == 60);
    assert(uiuctagActivityBucket(&real, 4) == 60);

    // slot timing: block opened late still maps slots to window boundaries
    const int32_t on_boundary = 1767225600;         // multiple of 7200
    assert(on_boundary % 7200 == 0);
    assert(uiuctagBlockStartEpoch(on_boundary) == on_boundary);
    assert(uiuctagSampleEpoch(on_boundary, 0) == on_boundary);
    assert(uiuctagSampleEpoch(on_boundary, 23) == on_boundary + 23 * 300);
    assert(uiuctagActivityBucketEpoch(on_boundary, 1, 4)
           == on_boundary + 300 + 240);

    const int32_t late = on_boundary + 1500;        // opened at slot 5
    assert(uiuctagBlockStartEpoch(late) == on_boundary);
    assert(uiuctagSampleEpoch(late, 5) == late);

    printf("S1 helpers: all assertions passed\n");
    return 0;
}
