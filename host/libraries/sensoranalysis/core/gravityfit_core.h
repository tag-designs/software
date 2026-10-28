/**
 * @file    gravityfit_core.h
 * @brief   Zero-g offset of an accelerometer, fitted from a calibration
 *          sweep, Qt-free.
 *
 * @details The implementation behind the Qt GravityFit class (gravityfit.h),
 *          which forwards here; moved onto sensormath types with results
 *          bit-identical to the Qt code (test/calibration_dump.cc).
 *
 *          At rest in any orientation an accelerometer reads gravity, so
 *          turning the tag traces a sphere of radius one g centred on the
 *          origin. A zero-g offset moves that sphere off the origin, and
 *          fitting its centre recovers the offset.
 *
 *          This matters because the offset is not small. The LIS2DU12
 *          datasheet gives +/- 11 mg typical after factory trim, but that is
 *          the bare part; soldering it to a board adds package stress, and two
 *          captures from one tag put the offset at 77 mg, dominated by the z
 *          axis -- the one perpendicular to the board. A fixed offset in the
 *          tag frame produces a tilt error that varies with orientation,
 *          maximal when the offset is perpendicular to gravity and zero when
 *          parallel, so it appears as scatter rather than bias. On those
 *          captures it accounts for about 3 degrees of the dip spread, and
 *          removing it improves that spread by 30 percent.
 *
 *          Deliberately a sphere and not an ellipsoid. Adding per-axis scale
 *          terms took the same captures from 30 to 34 percent, which does not
 *          pay for the extra machinery; the fitted radius comes out within one
 *          percent of one g, so the sensitivity constant is already right and
 *          only the offset is worth removing.
 *
 *          No buffer. A sphere fit is linear least squares, so the whole state
 *          is ten running sums and each sample is folded in and discarded.
 *
 * @see     accelcalibration_core.h, which weights the same fit by direction
 *          and is what qtcalibrate uses.
 */
#ifndef SENSORANALYSIS_CORE_GRAVITYFIT_CORE_H
#define SENSORANALYSIS_CORE_GRAVITYFIT_CORE_H

#include "vecmath.h"

namespace calibcore
{

/**
 * @class   GravityFit
 * @brief   Streaming least-squares sphere fit of accelerometer readings.
 *
 * @details Not thread-safe. Constant memory.
 */
class GravityFit
{
public:
    /**
     * @struct  Config
     * @brief   Intake gates and the sample minimum.
     */
    struct Config {
        /// Magnitude, as a fraction of one g, within which a sample is taken to
        /// be gravity rather than motion. Deliberately wide on the first pass:
        /// the offset being estimated is itself several percent, so a tight
        /// gate would reject orientations rather than movement.
        float gate = 0.15f;

        /// Gate for the second pass, once an offset estimate exists and the
        /// magnitude can be judged after correction.
        float refinedGate = 0.08f;

        /// Samples needed before the fit is offered at all.
        int minimumSamples = 50;
    };

    /**
     * @struct  Result
     * @brief   The fitted sphere.
     */
    struct Result {
        bool              valid = false;   ///< False: the other fields are not a fit.
        sensormath::Vec3f offset;          ///< Zero-g offset in the caller's units.
        float             radius = 0.0f;   ///< Fitted sphere radius; should be one g.
        float             residual = 0.0f; ///< RMS distance from the fitted sphere.
        int               samples = 0;     ///< Samples that passed the gate.
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
     *
     * @param[in] accel  Accelerometer vector in the caller's units.
     * @param[in] oneG   Magnitude corresponding to one g in those units.
     *
     * @return  false when the sample was outside the gate, non-finite, or
     *          oneG was not positive, and was ignored.
     */
    bool add(sensormath::Vec3f accel, float oneG);

    /**
     * @brief   Solves for the offset.
     *
     * @details Two passes are possible. The first uses everything inside the
     *          wide gate; the second re-gates on the magnitude after
     *          correction and solves again, which removes the circularity of
     *          gating on the quantity being estimated. The second pass needs
     *          the samples again, so callers wanting it must re-offer them --
     *          see refit().
     *
     * @param[in] oneG  One g in the caller's units; a fit whose radius is not
     *                  within 0.5 to 1.5 of it is refused. Not positive skips
     *                  that check.
     *
     * @return  The fit; invalid with fewer than minimumSamples samples, a
     *          singular system, or a radius outside the bounds above.
     */
    Result result(float oneG) const;

    /// Whether result() has enough samples to attempt a fit.
    bool ready() const { return count_ >= config_.minimumSamples; }

    /**
     * @brief   Starts a second pass around a known offset.
     *
     * @details Clears the sums and tightens the gate, shifting each subsequent
     *          sample by the given offset before testing it. Re-offer the
     *          samples, then call result() again.
     *
     * @param[in] offset  The first pass's offset.
     */
    void refit(sensormath::Vec3f offset);

    /// The configuration in force.
    const Config &config() const { return config_; }

private:
    Config config_; ///< Gates and minimum.

    // Normal equations for |p|^2 = 2 c.p + (r^2 - |c|^2), accumulated.
    double ata_[4][4];     ///< Sum of row * row^T.
    double atb_[4];        ///< Sum of row * |p|^2.
    double residualSum_;   ///< Sum of (|p|^2)^2, for the residual afterwards.
    int    count_;         ///< Samples accepted.

    sensormath::Vec3f pass2Offset_; ///< Offset the second pass corrects by.
    bool              refining_;    ///< In the second pass.
};

} // namespace calibcore

#endif // SENSORANALYSIS_CORE_GRAVITYFIT_CORE_H
