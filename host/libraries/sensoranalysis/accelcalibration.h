/**
 * @file    accelcalibration.h
 * @brief   Accelerometer zero-g calibration as a task of its own, on Qt types.
 *
 * @details An adapter over calibcore::AccelCalibration
 *          (core/accelcalibration_core.h), which holds the implementation and
 *          why the accelerometer has its own population, weighted by
 *          direction. Config is the core's own type; Result carries the offset
 *          as a QVector3D. Results are bit-identical to the Qt implementation
 *          this replaced.
 */
#ifndef ACCELCALIBRATION_H
#define ACCELCALIBRATION_H

#include "accelcalibration_core.h"

#include <QVector>
#include <QVector3D>

/**
 * @class   AccelCalibration
 * @brief   Direction-weighted sphere fit of accelerometer readings; see
 *          calibcore::AccelCalibration.
 */
class AccelCalibration
{
public:
    /// Lattice, gates and coverage preconditions; see
    /// calibcore::AccelCalibration::Config.
    using Config = calibcore::AccelCalibration::Config;

    /**
     * @struct  Result
     * @brief   The fit and the population it came from.
     */
    struct Result {
        bool      valid = false;   ///< False: offset, radius, residual are not a fit.
        QVector3D offset;          ///< Zero-g offset in the caller's units.
        float     radius = 0.0f;   ///< Fitted sphere radius; should be one g.
        float     residual = 0.0f; ///< RMS distance from the fitted sphere.

        int   samples = 0;         ///< Readings the fit was built from.
        int   patchesSeen = 0;     ///< Occupied patches.
        int   patches = 0;         ///< Patches in the lattice.
        float coverage = 0.0f;     ///< patchesSeen / patches.
        float isotropy = 0.0f;     ///< Direction evenness, 0 to 1.

        int   offered = 0;         ///< Readings offered since the last reset.
        int   gated = 0;           ///< Of those, rejected as motion.
    };

    /// With the default Config.
    AccelCalibration();

    /**
     * @brief   With a given configuration, clamped as Config describes.
     * @param[in] config  Lattice, gates and preconditions.
     */
    explicit AccelCalibration(const Config &config);

    /// Clears every patch and returns the gate to the wide one.
    void reset();

    /**
     * @brief   Offers one accelerometer reading.
     * @param[in] accel  Accelerometer vector in the caller's units.
     * @param[in] oneG   One g in those units.
     * @return  false when the reading was rejected as motion or was not finite.
     */
    bool add(const QVector3D &accel, float oneG);

    /**
     * @brief   Solves for the offset over every patch.
     * @param[in] oneG  One g in the caller's units.
     * @return  The fit and population figures.
     * @post    On a valid fit, later add() calls use the tighter gate around
     *          this offset; see calibcore::AccelCalibration::result().
     */
    Result result(float oneG);

    /**
     * @brief   Which direction patches have been visited, in lattice order.
     * @return  One entry per patch.
     */
    QVector<bool> occupancy() const;

    /// The configuration in force, after clamping.
    const Config &config() const { return core_.config(); }

private:
    calibcore::AccelCalibration core_; ///< The implementation.
};

#endif // ACCELCALIBRATION_H
