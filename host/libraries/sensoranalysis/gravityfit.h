/**
 * @file    gravityfit.h
 * @brief   Zero-g offset of an accelerometer, fitted from a calibration sweep,
 *          on Qt types.
 *
 * @details An adapter over calibcore::GravityFit (core/gravityfit_core.h),
 *          which holds the implementation and why the fit is a sphere. Config
 *          is the core's own type; Result carries the offset as a QVector3D.
 *          Results are bit-identical to the Qt implementation this replaced.
 */
#ifndef GRAVITYFIT_H
#define GRAVITYFIT_H

#include "gravityfit_core.h"

#include <QVector3D>

/**
 * @class   GravityFit
 * @brief   Streaming least-squares sphere fit of accelerometer readings; see
 *          calibcore::GravityFit.
 */
class GravityFit
{
public:
    /// Intake gates and the sample minimum; see calibcore::GravityFit::Config.
    using Config = calibcore::GravityFit::Config;

    /**
     * @struct  Result
     * @brief   The fitted sphere.
     */
    struct Result {
        bool      valid = false;   ///< False: the other fields are not a fit.
        QVector3D offset;          ///< Zero-g offset in the caller's units.
        float     radius = 0.0f;   ///< Fitted sphere radius; should be one g.
        float     residual = 0.0f; ///< RMS distance from the fitted sphere.
        int       samples = 0;     ///< Samples that passed the gate.
    };

    /// With the default Config.
    GravityFit();

    /**
     * @brief   With a given configuration.
     * @param[in] config  Gates and minimum.
     */
    explicit GravityFit(const Config &config);

    /// Clears the sums and leaves any second pass.
    void reset();

    /**
     * @brief   Offers one accelerometer sample.
     * @param[in] accel  Accelerometer vector in the caller's units.
     * @param[in] oneG   One g in those units.
     * @return  false when the sample was outside the gate and ignored.
     */
    bool add(const QVector3D &accel, float oneG);

    /**
     * @brief   Solves for the offset.
     * @param[in] oneG  One g in the caller's units.
     * @return  The fit; see calibcore::GravityFit::result() for when it is
     *          invalid.
     */
    Result result(float oneG) const;

    /// Whether result() has enough samples to attempt a fit.
    bool ready() const { return core_.ready(); }

    /**
     * @brief   Starts a second pass around a known offset: clears the sums,
     *          tightens the gate, and corrects each later sample by
     *          @p offset before testing it. Re-offer the samples, then call
     *          result() again.
     * @param[in] offset  The first pass's offset.
     */
    void refit(const QVector3D &offset);

    /// The configuration in force.
    const Config &config() const { return core_.config(); }

private:
    calibcore::GravityFit core_; ///< The implementation.
};

#endif // GRAVITYFIT_H
