#ifndef MAGRETENTION_H
#define MAGRETENTION_H

#include <QVector>
#include <QVector3D>

/*
 * Which magnetometer sample to discard when the calibration buffer is full.
 *
 * The inherited policy in choose_discard_magcal() has two unrelated branches:
 * a scan for the point furthest from the mean field strength, rate limited and
 * only active once coverage is already good, and otherwise a search for the two
 * closest points, discarding one of them at random. The second is O(N^2) --
 * about 211,000 distance evaluations for every sample added -- and both are
 * heuristics standing in for a question that has an exact answer.
 *
 * That question is which sample contributes least to determining the fit. For
 * the design matrix D whose rows are the solver's own ten-element measurement
 * vector, the leverage of sample i is
 *
 *     h_i = d_i^T (D^T D)^-1 d_i
 *
 * and removing row i scales det(D^T D) by (1 - h_i). So the least informative
 * sample is the one with the lowest leverage -- which is what "closest to
 * another point" gropes at, since a sample with a near neighbour is redundant,
 * but computed exactly and in O(p^2) with p = 10 rather than O(N^2).
 *
 * The same quantity gives the outlier test. An influential outlier has both
 * high leverage and a large residual, which Cook's distance combines:
 *
 *     D_i = (r_i^2 / (p s^2)) (h_i / (1 - h_i)^2)
 *
 * One framework replaces both branches, works at every sample count instead of
 * only once coverage is good, and needs no coverage figure at all -- so it also
 * drops the inherited policy's dependency on a gap metric refreshed on a
 * different timer than the one it is consulted from.
 *
 * Two guards against the feedback loop the inherited code's own comment warns
 * about, where a bad calibration drives discards that make it worse:
 *
 *   - a patch floor, so the last sample in an occupied patch is never evicted
 *     and coverage cannot be traded away for determinant; and
 *   - probation, so a newly added sample cannot be evicted immediately and a
 *     transient bad calibration cannot purge the evidence that would correct
 *     it.
 */
class MagRetention
{
public:
    struct Config {
        /// Cook's distance above which a sample is treated as an influential
        /// outlier and evicted ahead of any low-leverage sample. The
        /// conventional rule of thumb is 1.0.
        float outlierThreshold = 1.0f;

        /// Patches used for the coverage floor. Zero disables the floor.
        int patchCount = 100;

        /// Set false to evict purely on leverage, never on Cook's distance.
        /// Useful for isolating the two behaviours in a comparison.
        bool rejectOutliers = true;
    };

    enum class Reason {
        None,       ///< No choice could be made; the caller should fall back.
        Leverage,   ///< Evicted as the least informative sample.
        Outlier,    ///< Evicted as an influential outlier.
    };

    struct Choice {
        int    index = -1;
        Reason reason = Reason::None;
        float  score = 0.0f;   ///< Leverage, or Cook's distance for an outlier.
    };

    MagRetention();
    explicit MagRetention(const Config &config);

    /**
     * @brief Choose a sample to evict.
     *
     * @param samples      Calibrated sample vectors, one per occupied slot.
     * @param onProbation  Parallel flags; true means too recently added to be
     *                     a candidate. May be empty to protect nothing.
     *
     * @return The chosen index, or Reason::None when no choice is possible --
     *         too few samples, a rank-deficient design matrix, or every
     *         candidate protected. The caller decides what to do then; a
     *         rank-deficient fit is exactly the case where discarding on
     *         leverage would be guesswork.
     *
     * @details Does not modify anything. The caller owns the buffer.
     */
    Choice choose(const QVector<QVector3D> &samples,
                  const QVector<bool> &onProbation = QVector<bool>()) const;

    /**
     * @brief Leverage of every sample, for tests and diagnostics.
     *
     * @return Empty when the design matrix is rank deficient.
     */
    QVector<float> leverages(const QVector<QVector3D> &samples) const;

    const Config &config() const { return config_; }

    /// Elements in one design matrix row: the solver's measurement vector.
    static const int kParameters = 10;

private:
    Config config_;
};

#endif // MAGRETENTION_H
