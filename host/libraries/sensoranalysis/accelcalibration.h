#ifndef ACCELCALIBRATION_H
#define ACCELCALIBRATION_H

#include <QVector>
#include <QVector3D>


/*
 * Accelerometer calibration as a task of its own.
 *
 * The magnetometer and the accelerometer are calibrated by the same gesture --
 * the operator turns the tag until its readings cover a sphere -- but they are
 * two independent fits, and giving them one sample population serves neither.
 *
 * They were sharing one before this. Each accelerometer reading was stored in
 * whatever slot the magnetometer's retention policy had chosen, and the
 * gravity fit ran over whatever survived. That coupling runs the wrong way in
 * two senses. The accelerometer contributes nothing to the magnetometer's
 * decision, so there is no feedback to preserve; and the samples the
 * magnetometer discards first are the ones the accelerometer most wants. A
 * rotation about the field axis leaves the field unchanged in body
 * coordinates, so it is magnetometrically redundant and evicted early, while
 * being exactly the motion that carries gravity to a new direction.
 *
 * Their intake gates also reject on different physics. This one rejects
 * motion: a reading taken mid-swing is not gravity. A magnetometer outlier is
 * field distortion: a reading taken beside a steel bench leg. A sample that
 * fails one is usually fine for the other, so one shared gate throws away
 * data both tasks had a use for.
 *
 * What the two still share is the tag and the gesture, and one derived
 * quantity -- the angle between field and gravity -- which needs a
 * magnetometer and an accelerometer reading from the same instant. That
 * pairing is the caller's to keep; it is not a calibration input and does not
 * belong in either task's buffer.
 *
 * There is no buffer and nothing is ever discarded. A sphere fit is linear
 * least squares, so its normal equations are sums over the samples -- which
 * is why GravityFit needs no buffer either. The only thing the direction
 * patches were ever for is to stop a direction the operator dwelt on from
 * outweighing one they passed through quickly, and that is a question of
 * weight, not of which samples to keep.
 *
 * So each patch accumulates its own normal equations and its own count, and
 * the fit divides each patch's contribution by its count before summing them.
 * Every sample contributes, every occupied direction counts once however long
 * the operator lingered there, the estimate converges as the counts grow
 * instead of jittering as a window slides, and the whole state is a few
 * hundred bytes a patch with no eviction rule to get wrong.
 *
 * The rule this replaced kept the most recent samples per patch. That made
 * the fit a sliding window that never settled: on the reference capture the
 * offset wandered 13 mg peak to peak over the second half of a sweep, which
 * is 0.76 degrees of tilt. Keeping the oldest instead would have frozen it
 * once the patches filled, which is no better -- it throws away everything
 * the operator does after that, and sweeping longer should go on helping.
 *
 * Averaging each patch's samples into one point would not do either: a patch
 * spans about twenty degrees, so the mean of readings across it falls inside
 * the sphere rather than on it, which biases the radius by a percent or so.
 * Accumulating the normal equations has no such bias, because it is still
 * exact least squares over every individual sample -- only the weights
 * change.
 */
class AccelCalibration
{
public:
    struct Config {
        /// Patches of the gravity-direction sphere.
        int patches = 32;

        /// Magnitude, as a fraction of one g, within which a reading is taken
        /// to be gravity rather than motion. Deliberately wide: the offset
        /// being estimated is itself several percent of one g, so a tight
        /// gate at intake would reject orientations rather than movement.
        float gate = 0.15f;

        /// Once a fit exists, readings are judged on the magnitude left after
        /// correcting by it, and this tighter gate applies.
        ///
        /// The wide gate has to stay wide until then: the offset being
        /// estimated is itself several percent of one g, so judging a raw
        /// magnitude tightly would reject orientations rather than movement.
        /// Once an offset is known that confound is gone, and on the
        /// reference capture the difference is worth about 5 mg -- a third of
        /// a degree of tilt -- because the wide gate admits readings taken
        /// mid-swing.
        ///
        /// This is the streaming form of the second pass a buffered fit can
        /// make retroactively. It cannot run away: the gate only ever
        /// tightens once a fit has passed the coverage preconditions, by
        /// which point the offset is good to a few milli-g, and a few milli-g
        /// of error moves a corrected magnitude by far less than the margin
        /// between the two gates.
        float refinedGate = 0.08f;

        /// Occupied patches needed before a fit is offered. A sphere fit from
        /// a concentrated cloud is numerically fine and physically
        /// meaningless, and nothing in the solver can tell the difference --
        /// which is why this precondition lives here, with the population,
        /// rather than in GravityFit.
        int minimumPatches = 12;

        /// Direction evenness below which a fit is refused. Catches the sweep
        /// that occupies enough patches but lies in a plane: tipping the tag
        /// through one arc gives a great circle of gravity directions, which
        /// a patch count alone reports as broad coverage.
        float minimumIsotropy = 0.05f;
    };

    struct Result {
        bool      valid = false;
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

    AccelCalibration();
    explicit AccelCalibration(const Config &config);

    void reset();

    /**
     * @brief Offer one accelerometer reading.
     *
     * @param accel  Accelerometer vector in the caller's units.
     * @param oneG   Magnitude corresponding to one g in those units.
     *
     * @return false when the reading was rejected as motion or was not
     *         finite. Nothing is ever displaced: an accepted reading is folded
     *         into its patch's normal equations and the vector itself is
     *         discarded.
     */
    bool add(const QVector3D &accel, float oneG);

    /**
     * @brief Solve for the offset over the buffer.
     *
     * @details Each occupied patch's accumulated normal equations are divided
     *          by its count and summed, so every occupied direction carries
     *          the same weight however long the operator spent there, and
     *          then solved. Cheap enough to call on every tick.
     *
     *          Invalid until the coverage preconditions are met, so a caller
     *          can apply the offset whenever `valid` is set without testing
     *          anything else.
     */
    Result result(float oneG);

    const Config &config() const { return config_; }

private:
    Config config_;

    /// One patch's share of the fit. Normal equations for
    /// |p|^2 = 2 c.p + (r^2 - |c|^2), accumulated over the samples that landed
    /// in this patch, plus what the residual needs.
    struct Patch {
        double ata[4][4];
        double atb[4];
        double rhsSquares;   ///< Sum of (|p|^2)^2, for the residual.
        /// Second moment of the unit directions in this patch, for evenness.
        /// Kept per patch and weighted like the rest, so that a long dwell
        /// cannot read as coverage -- and kept at full angular resolution
        /// rather than collapsing each patch to its centre, because a sweep
        /// confined to one plane has to read as flat however the lattice
        /// happens to straddle it.
        double moment[3][3];
        int    count;
    };

    QVector<Patch> patches_;

    int offered_;
    int gated_;

    /// Last good offset, used to tighten the intake gate. Not const-queried:
    /// result() refreshes it, which is why result() is not const.
    QVector3D gateOffset_;
    bool      haveGateOffset_;
};

#endif // ACCELCALIBRATION_H
