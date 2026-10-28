/**
 * @file    magquality.h
 * @brief   Calibration quality metrics for a set of magnetometer samples, on
 *          Qt types.
 *
 * @details An adapter over calibcore::MagQuality (core/magquality_core.h),
 *          which holds the implementation, the metrics' definitions and their
 *          rationale. Config and Result are the core's own types. Results are
 *          bit-identical to the Qt implementation this replaced.
 *
 *          Usage recomputes from the sample buffer rather than updating
 *          incrementally:
 *
 *              MagQuality q;
 *              for (each valid buffer slot)
 *                  q.add(calibratedMag, accelOrNull, dipDegrees, oneG);
 *              const MagQuality::Result r = q.result();
 */
#ifndef MAGQUALITY_H
#define MAGQUALITY_H

#include "magquality_core.h"

#include <QVector3D>

/**
 * @class   MagQuality
 * @brief   Coverage, fit residual, attitude diversity and dip consistency of
 *          calibrated magnetometer samples; see calibcore::MagQuality.
 */
class MagQuality
{
public:
    /// Lattice sizes and the accelerometer gate; see calibcore::MagQuality::Config.
    using Config = calibcore::MagQuality::Config;
    /// Every metric; see calibcore::MagQuality::Result.
    using Result = calibcore::MagQuality::Result;

    /// With the default Config.
    MagQuality();

    /**
     * @brief   With a given configuration, clamped as Config describes.
     * @param[in] config  Lattice sizes and gate.
     */
    explicit MagQuality(const Config &config);

    /// Drops every sample, keeping the configuration.
    void reset();

    /**
     * @brief   Offers one sample.
     *
     * @param[in] mag         Calibrated magnetometer vector; must be non-zero.
     * @param[in] accel       Accelerometer vector in the caller's units, or
     *                        nullptr when the sample has none.
     * @param[in] dipDegrees  Inclination for this sample as CompassProcessor
     *                        derived it. Ignored when accel is nullptr.
     * @param[in] oneG        One g in the units of accel; 1000 for milli-g.
     *                        Ignored when accel is nullptr.
     *
     * @return  false when the sample was rejected outright: a zero-length or
     *          non-finite magnetometer vector.
     *
     * @see     calibcore::MagQuality::add()
     */
    bool add(const QVector3D &mag, const QVector3D *accel = nullptr,
             float dipDegrees = 0.0f, float oneG = 1.0f);

    /**
     * @brief   Computes every metric from the samples offered so far.
     * @return  The metrics.
     */
    Result result() const;

    /// The configuration in force, after clamping.
    const Config &config() const { return core_.config(); }

    /**
     * @brief   Index of the patch containing a direction, or -1 if unusable.
     * @param[in] v           Any vector; only its direction is used.
     * @param[in] patchCount  Lattice size.
     * @return  The patch, or -1 for a zero or non-finite vector.
     * @see     Directions::patch()
     */
    static int patch(const QVector3D &v, int patchCount);

    /**
     * @brief   Centre of a patch, as a unit vector.
     * @param[in] index       Patch index.
     * @param[in] patchCount  Lattice size.
     * @return  The centre, or a null vector outside the lattice.
     * @see     Directions::patchCenter()
     */
    static QVector3D patchCenter(int index, int patchCount);

private:
    calibcore::MagQuality core_; ///< The implementation.
};

#endif // MAGQUALITY_H
