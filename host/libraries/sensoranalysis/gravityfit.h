#ifndef GRAVITYFIT_H
#define GRAVITYFIT_H

#include <QVector3D>

/*
 * Zero-g offset of an accelerometer, fitted from a calibration sweep.
 *
 * At rest in any orientation an accelerometer reads gravity, so turning the tag
 * traces a sphere of radius one g centred on the origin. A zero-g offset moves
 * that sphere off the origin, and fitting its centre recovers the offset.
 *
 * This matters because the offset is not small. The LIS2DU12 datasheet gives
 * +/- 11 mg typical after factory trim, but that is the bare part; soldering it
 * to a board adds package stress, and two captures from one tag put the offset
 * at 77 mg, dominated by the z axis -- the one perpendicular to the board. A
 * fixed offset in the tag frame produces a tilt error that varies with
 * orientation, maximal when the offset is perpendicular to gravity and zero
 * when parallel, so it appears as scatter rather than bias. On those captures
 * it accounts for about 3 degrees of the dip spread, and removing it improves
 * that spread by 30 percent.
 *
 * Deliberately a sphere and not an ellipsoid. Adding per-axis scale terms took
 * the same captures from 30 to 34 percent, which does not pay for the extra
 * machinery; the fitted radius comes out within one percent of one g, so the
 * sensitivity constant is already right and only the offset is worth removing.
 *
 * No buffer. A sphere fit is linear least squares, so the whole state is ten
 * running sums and each sample is folded in and discarded. The magnetometer
 * needs a buffer because it refits ten parameters repeatedly and must be able
 * to remove a sample again; gravity has constant magnitude by definition and
 * four parameters determine it.
 */
class GravityFit
{
public:
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

    struct Result {
        bool      valid = false;
        QVector3D offset;          ///< Zero-g offset in the caller's units.
        float     radius = 0.0f;   ///< Fitted sphere radius; should be one g.
        float     residual = 0.0f; ///< RMS distance from the fitted sphere.
        int       samples = 0;     ///< Samples that passed the gate.
    };

    GravityFit();
    explicit GravityFit(const Config &config);

    void reset();

    /**
     * @brief Offer one accelerometer sample.
     *
     * @param accel  Accelerometer vector in the caller's units.
     * @param oneG   Magnitude corresponding to one g in those units.
     *
     * @return false when the sample was outside the gate and ignored.
     */
    bool add(const QVector3D &accel, float oneG);

    /**
     * @brief Solve for the offset.
     *
     * @details Two passes. The first uses everything inside the wide gate; the
     *          second re-gates on the magnitude after correction and solves
     *          again, which removes the circularity of gating on the quantity
     *          being estimated. The second pass needs the samples again, so
     *          callers wanting it must re-offer them -- see refit().
     */
    Result result(float oneG) const;

    /// Whether result() would return a valid fit.
    bool ready() const { return count_ >= config_.minimumSamples; }

    /**
     * @brief Start a second pass around a known offset.
     *
     * @details Clears the sums and tightens the gate, shifting each subsequent
     *          sample by the given offset before testing it. Re-offer the
     *          samples, then call result() again.
     */
    void refit(const QVector3D &offset);

    const Config &config() const { return config_; }

private:
    Config config_;

    // Normal equations for |p|^2 = 2 c.p + (r^2 - |c|^2), accumulated.
    double ata_[4][4];
    double atb_[4];
    double residualSum_;   ///< Sum of (|p|^2)^2, for the residual afterwards.
    int    count_;

    QVector3D pass2Offset_;
    bool      refining_;
};

#endif // GRAVITYFIT_H
