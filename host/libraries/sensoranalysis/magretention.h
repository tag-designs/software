/**
 * @file    magretention.h
 * @brief   Which magnetometer sample to discard when the calibration buffer is
 *          full, on Qt types.
 *
 * @details An adapter over calibcore::MagRetention (core/magretention_core.h),
 *          which holds the implementation and the reasoning behind leverage
 *          and the studentized residual. Config, Reason and Choice are the
 *          core's own types. Results are bit-identical to the Qt
 *          implementation this replaced.
 */
#ifndef MAGRETENTION_H
#define MAGRETENTION_H

#include "magretention_core.h"

#include <QVector>
#include <QVector3D>

/**
 * @class   MagRetention
 * @brief   Chooses the sample to evict from a full magnetometer buffer; see
 *          calibcore::MagRetention.
 */
class MagRetention
{
public:
    /// Outlier threshold and coverage floor; see calibcore::MagRetention::Config.
    using Config = calibcore::MagRetention::Config;
    /// Why a sample was chosen.
    using Reason = calibcore::MagRetention::Reason;
    /// The sample chosen, and why.
    using Choice = calibcore::MagRetention::Choice;

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
     * @param[in] samples      Calibrated sample vectors, one per occupied slot.
     * @param[in] onProbation  Parallel flags; true means too recently added to
     *                         be a candidate. May be empty to protect nothing.
     *
     * @return  The chosen index, or Reason::None when no choice is possible:
     *          too few samples, a rank-deficient design matrix, or every
     *          candidate protected. Modifies nothing; the caller owns the
     *          buffer.
     *
     * @see     calibcore::MagRetention::choose()
     */
    Choice choose(const QVector<QVector3D> &samples,
                  const QVector<bool> &onProbation = QVector<bool>()) const;

    /**
     * @brief   Leverage of every sample, for tests and diagnostics.
     * @param[in] samples  As for choose().
     * @return  One per sample; empty when the design matrix is rank deficient.
     */
    QVector<float> leverages(const QVector<QVector3D> &samples) const;

    /// The configuration in force.
    const Config &config() const { return core_.config(); }

    /// Elements in one design matrix row: the solver's measurement vector.
    static const int kParameters = calibcore::MagRetention::kParameters;

private:
    calibcore::MagRetention core_; ///< The implementation.
};

#endif // MAGRETENTION_H
