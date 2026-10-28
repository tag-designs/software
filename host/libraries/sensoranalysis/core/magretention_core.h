/**
 * @file    magretention_core.h
 * @brief   Which magnetometer sample to discard when the calibration buffer
 *          is full, Qt-free.
 *
 * @details The implementation behind the Qt MagRetention class
 *          (magretention.h), which forwards here; moved onto sensormath types
 *          with results bit-identical to the Qt code
 *          (test/calibration_dump.cc).
 *
 *          The inherited policy in choose_discard_magcal() had two unrelated
 *          branches: a scan for the point furthest from the mean field
 *          strength, rate limited and only active once coverage was already
 *          good, and otherwise a search for the two closest points, discarding
 *          one of them at random. The second was O(N^2) -- about 211,000
 *          distance evaluations for every sample added -- and both were
 *          heuristics standing in for a question that has an exact answer.
 *
 *          That question is which sample contributes least to determining the
 *          fit. For the design matrix D whose rows are the solver's own
 *          ten-element measurement vector, the leverage of sample i is
 *
 *              h_i = d_i^T (D^T D)^-1 d_i
 *
 *          and removing row i scales det(D^T D) by (1 - h_i). So the least
 *          informative sample is the one with the lowest leverage -- which is
 *          what "closest to another point" gropes at, since a sample with a
 *          near neighbour is redundant, but computed exactly and in O(p^2)
 *          with p = 10 rather than O(N^2).
 *
 *          The outlier test is a separate question, and deliberately not
 *          Cook's distance. Cook's measures influence -- how far the fit moves
 *          if the sample is dropped -- which is residual multiplied by
 *          leverage, and leverage is already doing a job here. Using it twice
 *          points it in opposite directions: high leverage means keep under
 *          the rule above, and means suspicious under Cook's, so the most
 *          informative samples are the ones it accuses.
 *
 *          The question worth asking is narrower -- is this reading wrong? --
 *          and that is the studentized residual,
 *
 *              t_i = r_i / (s sqrt(1 - h_i))
 *
 *          which is in standard deviations and says nothing about how useful
 *          the sample is. Its threshold is also interpretable, where Cook's
 *          was not: the textbook D > 1 needs a 25-sigma residual at n = 650
 *          and p = 10, because the mean leverage is p/n = 0.015, which is why
 *          that rule never once fired on real captures. It was unreachable
 *          rather than strict.
 *
 *          One framework replaces both branches, works at every sample count
 *          instead of only once coverage is good, and needs no coverage figure
 *          at all.
 *
 *          Two guards against the feedback loop the inherited code's own
 *          comment warns about, where a bad calibration drives discards that
 *          make it worse:
 *
 *            - a patch floor, so the last sample in an occupied patch is never
 *              evicted and coverage cannot be traded away for determinant; and
 *            - probation, so a newly added sample cannot be evicted
 *              immediately and a transient bad calibration cannot purge the
 *              evidence that would correct it. Probation is the caller's to
 *              track and pass in.
 */
#ifndef SENSORANALYSIS_CORE_MAGRETENTION_CORE_H
#define SENSORANALYSIS_CORE_MAGRETENTION_CORE_H

#include "vecmath.h"

#include <vector>

namespace calibcore
{

/**
 * @class   MagRetention
 * @brief   Chooses the sample to evict from a full magnetometer buffer, by
 *          leverage, or ahead of that as an outlier.
 *
 * @details Stateless apart from its configuration: every call works on the
 *          samples it is given and modifies nothing. Safe to call from any
 *          thread on its own instance.
 */
class MagRetention
{
public:
    /**
     * @struct  Config
     * @brief   Outlier threshold and the coverage floor.
     */
    struct Config {
        /// Studentized residual, in standard deviations, above which a sample
        /// is treated as a bad reading and evicted ahead of any low-leverage
        /// one.
        ///
        /// Set from a false-positive budget rather than convention. At n = 650
        /// a threshold of 4 is about 0.04 expected false rejections per run
        /// and 3.5 about 0.3, so 4 means the rule essentially never fires by
        /// chance and fires on a reading that is genuinely inconsistent with
        /// the sphere.
        float outlierSigma = 4.0f;

        /// Patches used for the coverage floor. Zero disables the floor.
        int patchCount = 100;

        /// Set false to evict purely on leverage, never as an outlier.
        /// Useful for isolating the two behaviours in a comparison, and for a
        /// buffer not yet calibrated, where "wrong magnitude" has no meaning.
        bool rejectOutliers = true;
    };

    /// Why a sample was chosen.
    enum class Reason {
        None,       ///< No choice could be made; the caller should fall back.
        Leverage,   ///< Evicted as the least informative sample.
        Outlier,    ///< Evicted as a reading inconsistent with the fit.
    };

    /**
     * @struct  Choice
     * @brief   The sample chosen, and why.
     */
    struct Choice {
        int    index = -1;              ///< Index into the samples, or -1.
        Reason reason = Reason::None;   ///< None when index is -1.
        float  score = 0.0f;            ///< Leverage, or sigmas for an outlier.
    };

    /// With the default Config.
    MagRetention();

    /**
     * @brief   With a given configuration.
     * @param[in] config  Threshold and floor.
     */
    explicit MagRetention(const Config &config);

    /**
     * @brief   Chooses a sample to evict.
     *
     * @param[in] samples      Calibrated sample vectors, one per occupied
     *                         slot, in any units.
     * @param[in] onProbation  Parallel flags; true means too recently added
     *                         to be a candidate. May be shorter than samples,
     *                         or empty, to protect nothing beyond its end.
     *
     * @return  The chosen index, or Reason::None when no choice is possible --
     *          ten samples or fewer, a rank-deficient design matrix, or every
     *          candidate protected. The caller decides what to do then; a
     *          rank-deficient fit is exactly the case where discarding on
     *          leverage would be guesswork.
     */
    Choice choose(const std::vector<sensormath::Vec3f> &samples,
                  const std::vector<bool> &onProbation = std::vector<bool>()) const;

    /**
     * @brief   Leverage of every sample, for tests and diagnostics.
     * @param[in] samples  As for choose().
     * @return  One leverage per sample, or empty when there are ten samples or
     *          fewer or the design matrix is rank deficient.
     */
    std::vector<float> leverages(const std::vector<sensormath::Vec3f> &samples) const;

    /// The configuration in force.
    const Config &config() const { return config_; }

    /// Elements in one design matrix row: the solver's measurement vector.
    static const int kParameters = 10;

private:
    Config config_; ///< Threshold and floor.
};

} // namespace calibcore

#endif // SENSORANALYSIS_CORE_MAGRETENTION_CORE_H
