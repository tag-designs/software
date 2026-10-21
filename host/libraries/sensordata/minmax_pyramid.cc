/**
 * @file    minmax_pyramid.cc
 * @brief   Building and querying the sensordata min/max pyramid.
 *
 * @details See minmax_pyramid.h for the layout. The query is a greedy
 *          left-to-right walk: at each position it takes the largest bucket
 *          that starts there, ends inside the range and exists, and otherwise
 *          one raw sample. Because the walk is left to right and only a
 *          strictly better value replaces the current extreme, the bucket
 *          that supplies an extreme is the first one holding it, and the
 *          descent then finds the first sample within that bucket.
 */
#include "minmax_pyramid.h"

#include <algorithm>
#include <cmath>

namespace sensordata {
namespace detail {

namespace {

/// Smaller of two bucket minima, treating NaN as "no value"; keeps @p a on a tie.
float combineMin(float a, float b)
{
    if (std::isnan(a)) {
        return b;
    }
    if (std::isnan(b)) {
        return a;
    }
    return b < a ? b : a;
}

/// Larger of two bucket maxima, treating NaN as "no value"; keeps @p a on a tie.
float combineMax(float a, float b)
{
    if (std::isnan(a)) {
        return b;
    }
    if (std::isnan(b)) {
        return a;
    }
    return b > a ? b : a;
}

/// Number of trailing zero bits of a non-zero @p value.
unsigned trailingZeros(std::size_t value)
{
    unsigned count = 0;
    while ((value & 1u) == 0) {
        value >>= 1;
        count++;
    }
    return count;
}

/// Where a running extreme came from: one raw sample, or a pyramid bucket.
struct Source {
    bool raw = true;         ///< True for a raw sample.
    std::size_t level = 0;   ///< Pyramid level of the bucket.
    std::size_t index = 0;   ///< Sample index (raw) or bucket index.
};

} // namespace

void MinMaxPyramid::build(const float *values, std::size_t count)
{
    levels_.clear();
    const std::size_t buckets = count / kBaseBucket;
    if (buckets == 0) {
        return;
    }

    std::vector<float> base(buckets * 2);
    for (std::size_t b = 0; b < buckets; b++) {
        const float *bucket = values + b * kBaseBucket;
        float lo = std::nanf("");
        float hi = std::nanf("");
        for (std::size_t i = 0; i < kBaseBucket; i++) {
            lo = combineMin(lo, bucket[i]);
            hi = combineMax(hi, bucket[i]);
        }
        base[2 * b] = lo;
        base[2 * b + 1] = hi;
    }
    levels_.push_back(std::move(base));

    while (levels_.back().size() / 2 >= 2) {
        const std::vector<float> &below = levels_.back();
        const std::size_t count_above = below.size() / 4;
        std::vector<float> above(count_above * 2);
        for (std::size_t b = 0; b < count_above; b++) {
            above[2 * b] = combineMin(below[4 * b], below[4 * b + 2]);
            above[2 * b + 1] = combineMax(below[4 * b + 1], below[4 * b + 3]);
        }
        levels_.push_back(std::move(above));
    }
}

MinMaxPyramid::Extremes MinMaxPyramid::query(const float *values, std::size_t begin,
                                             std::size_t end, bool locate) const
{
    Extremes result;
    Source min_source;
    Source max_source;

    auto consider = [&](float lo, float hi, const Source &source) {
        if (std::isnan(lo)) {
            return;
        }
        if (!result.valid) {
            result.valid = true;
            result.min = lo;
            result.max = hi;
            min_source = source;
            max_source = source;
            return;
        }
        if (lo < result.min) {
            result.min = lo;
            min_source = source;
        }
        if (hi > result.max) {
            result.max = hi;
            max_source = source;
        }
    };

    const std::size_t level_count = levels_.size();
    std::size_t i = begin;
    while (i < end) {
        if (level_count > 0 && i % kBaseBucket == 0) {
            // Bucket i/kBaseBucket of level 0 is aligned to level k when its
            // low k bits are zero; try the coarsest such level first.
            const std::size_t base_bucket = i / kBaseBucket;
            std::size_t top = level_count - 1;
            if (base_bucket != 0) {
                top = std::min<std::size_t>(top, trailingZeros(base_bucket));
            }
            bool taken = false;
            for (std::size_t k = top + 1; k-- > 0;) {
                const std::size_t span = kBaseBucket << k;
                const std::size_t bucket = base_bucket >> k;
                if (i + span <= end && bucket < levels_[k].size() / 2) {
                    consider(levels_[k][2 * bucket], levels_[k][2 * bucket + 1],
                             Source{false, k, bucket});
                    i += span;
                    taken = true;
                    break;
                }
            }
            if (taken) {
                continue;
            }
        }
        consider(values[i], values[i], Source{true, 0, i});
        i++;
    }

    if (locate && result.valid) {
        result.min_index = min_source.raw
            ? min_source.index
            : this->locate(values, min_source.level, min_source.index, result.min, true);
        result.max_index = max_source.raw
            ? max_source.index
            : this->locate(values, max_source.level, max_source.index, result.max, false);
    }
    return result;
}

std::size_t MinMaxPyramid::locate(const float *values, std::size_t level, std::size_t bucket,
                                  float target, bool minimum) const
{
    // Every child of an existing bucket exists, because each level pairs
    // whole buckets of the one below. Prefer the left child, so the first
    // matching sample is found.
    const std::size_t slot = minimum ? 0 : 1;
    for (std::size_t k = level; k > 0; k--) {
        const std::size_t left = 2 * bucket;
        bucket = levels_[k - 1][2 * left + slot] == target ? left : left + 1;
    }
    const std::size_t first = bucket * kBaseBucket;
    for (std::size_t i = first; i < first + kBaseBucket; i++) {
        if (values[i] == target) {
            return i;
        }
    }
    // Unreachable while the pyramid matches @p values.
    return first;
}

std::size_t MinMaxPyramid::bytes() const
{
    std::size_t total = 0;
    for (const std::vector<float> &level : levels_) {
        total += level.size() * sizeof(float);
    }
    return total;
}

} // namespace detail
} // namespace sensordata
