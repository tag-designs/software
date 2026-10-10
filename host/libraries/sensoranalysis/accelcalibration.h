#ifndef ACCELCALIBRATION_H
#define ACCELCALIBRATION_H

#include <QVector>
#include <QVector3D>

#include "gravityfit.h"

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
 * The buffer here is small and needs no eviction policy. Four parameters
 * determine a sphere, there are no cross terms to resolve, and what the fit
 * wants is direction spread rather than density -- so each patch of the
 * direction sphere gets a fixed, equal number of slots and keeps its most
 * recent arrivals. Coverage is then uniform by construction, the cost is O(1)
 * per sample with no search, and a patch the operator lingers in cannot crowd
 * out one they passed through quickly. Keeping the newest also lets a long
 * sweep track thermal drift rather than averaging it in.
 */
class AccelCalibration
{
public:
    struct Config {
        /// Patches of the gravity-direction sphere.
        int patches = 32;

        /// Slots per patch. The buffer is patches * perPatch samples; at the
        /// defaults, 256. The offset's standard error goes as sigma/sqrt(n),
        /// and with the 31 mg residual measured on the reference captures
        /// that is under 2 mg at this size -- about a tenth of a degree of
        /// tilt, well inside what the sphere model itself costs. More samples
        /// would not help: beyond the noise the residual is model error, and
        /// that does not average down.
        int perPatch = 8;

        /// Magnitude, as a fraction of one g, within which a reading is taken
        /// to be gravity rather than motion. Deliberately wide: the offset
        /// being estimated is itself several percent of one g, so a tight
        /// gate at intake would reject orientations rather than movement.
        float gate = 0.15f;

        /// Gate for the solver's second pass, once an offset estimate exists
        /// and the magnitude can be judged after correction.
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

        int   samples = 0;         ///< Readings held in the buffer.
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
     *         finite. A reading that is kept may still displace an older one
     *         from its patch.
     */
    bool add(const QVector3D &accel, float oneG);

    /**
     * @brief Solve for the offset over the buffer.
     *
     * @details Two passes, as GravityFit describes: a wide gate first, then a
     *          re-gate on the corrected magnitude. Both passes run over the
     *          same held samples, so unlike a streaming fit the second pass
     *          costs nothing extra to arrange.
     *
     *          Invalid until the coverage preconditions are met, so a caller
     *          can apply the offset whenever `valid` is set without testing
     *          anything else.
     */
    Result result(float oneG) const;

    const Config &config() const { return config_; }

private:
    Config config_;

    /// patches * perPatch, patch-major. A slot is live when its index is less
    /// than fill_ for that patch.
    QVector<QVector3D> slots_;
    QVector<int>       fill_;   ///< Live slots in each patch.
    QVector<int>       next_;   ///< Ring cursor within each patch.

    int offered_;
    int gated_;
};

#endif // ACCELCALIBRATION_H
