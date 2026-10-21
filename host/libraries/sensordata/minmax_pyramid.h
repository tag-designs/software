/**
 * @file    minmax_pyramid.h
 * @brief   Min/max pyramid over one stream's `float` values (internal to sensordata).
 *
 * @details Level 0 holds the minimum and maximum of every complete
 *          kBaseBucket-sample bucket; each level above pairs adjacent buckets
 *          of the one below, so bucket `b` of level `k` covers samples
 *          `[b * (kBaseBucket << k), (b + 1) * (kBaseBucket << k))`. A
 *          trailing partial bucket, and a trailing unpaired bucket at any
 *          level, is left out; queries scan or descend to cover it. The
 *          pyramid costs about `n / 2` bytes for `n` samples.
 *
 *          NaN samples are ignored. A bucket holding only NaN stores NaN as
 *          both its minimum and maximum, which every comparison then skips.
 *
 *          Each level is one contiguous array of interleaved `float`
 *          min/max pairs, the same layout the proposal gives a stored
 *          pyramid, so one read from disk could replace a built one.
 */
#ifndef SENSORDATA_MINMAX_PYRAMID_H
#define SENSORDATA_MINMAX_PYRAMID_H

#include <cstddef>
#include <vector>

namespace sensordata {
namespace detail {

/**
 * @class   MinMaxPyramid
 * @brief   Answers min/max over any sample range in O(log n) bucket reads.
 *
 * @details Holds only the aggregates; every query is passed the value array
 *          the pyramid was built from, which must not have changed since.
 */
class MinMaxPyramid {
public:
    /// Samples per level-0 bucket.
    static constexpr std::size_t kBaseBucket = 32;

    /**
     * @struct  Extremes
     * @brief   Result of query().
     */
    struct Extremes {
        bool valid = false;         ///< False when the range held no non-NaN sample; the other fields are then unset.
        float min = 0.0f;           ///< Smallest value.
        float max = 0.0f;           ///< Largest value.
        std::size_t min_index = 0;  ///< First sample equal to #min; set only when located.
        std::size_t max_index = 0;  ///< First sample equal to #max; set only when located.
    };

    /**
     * @brief   Builds the pyramid, replacing any previous contents.
     * @param[in] values  Sample values.
     * @param[in] count   Number of samples.
     */
    void build(const float *values, std::size_t count);

    /**
     * @brief   Minimum and maximum of samples `[begin, end)`.
     *
     * @details Combines the largest whole buckets that fit, scanning raw
     *          samples only where the range is not bucket-aligned. When
     *          @p locate is set, the bucket that supplied each extreme is
     *          descended to level 0 to find the index of the first sample
     *          holding it, which costs one step per level plus one bucket
     *          scan. "First" is by index, matching a left-to-right scan with
     *          strict comparisons; -0 and +0 compare equal.
     *
     * @param[in] values  The array the pyramid was built from.
     * @param[in] begin   First sample, inclusive.
     * @param[in] end     Last sample, exclusive; at most the built count.
     * @param[in] locate  Also find Extremes::min_index and max_index.
     *
     * @return  The extremes; Extremes::valid is false for an empty or all-NaN
     *          range.
     */
    Extremes query(const float *values, std::size_t begin, std::size_t end, bool locate) const;

    /// Bytes held by all levels.
    std::size_t bytes() const;

private:
    /// Index of the first sample in level-@p level bucket @p bucket equal to @p target, by descent.
    std::size_t locate(const float *values, std::size_t level, std::size_t bucket, float target,
                       bool minimum) const;

    /// Interleaved min/max pairs, one vector per level, finest first.
    std::vector<std::vector<float>> levels_;
};

} // namespace detail
} // namespace sensordata

#endif // SENSORDATA_MINMAX_PYRAMID_H
