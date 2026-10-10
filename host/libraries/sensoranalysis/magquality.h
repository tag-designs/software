#ifndef MAGQUALITY_H
#define MAGQUALITY_H

#include <QVector>
#include <QVector3D>

/*
 * Calibration quality metrics for a set of magnetometer samples.
 *
 * This is ours, unlike the inherited metrics in qtcalibrate/magcal/quality.c
 * that it is intended to replace. It is computed alongside them while the two
 * are compared; see
 * host/docs/design/proposals/qtcalibrate-quality-replacement.md.
 *
 * The question the inherited metrics answer is how well a fitted ellipsoid
 * reproduces the samples it was fitted to, which a bad calibration can satisfy.
 * These try to answer whether the samples determine the calibration at all, and
 * -- through the accelerometer -- to say something the solver is not optimising
 * and therefore cannot flatter.
 *
 * Usage recomputes from the sample buffer rather than updating incrementally,
 * which is what the application already does and leaves nothing to go stale:
 *
 *     MagQuality q;
 *     for (each valid buffer slot)
 *         q.add(calibratedMag, accelOrNull, dipDegrees, oneG);
 *     const MagQuality::Result r = q.result();
 *
 * Frames: this class does no frame conversion and must not start. The
 * magnetometer and the accelerometer do not share an axis convention -- see
 * docs/shared/sensor-axes.md -- and resolving that is CompassProcessor's job.
 * The caller passes the inclination CompassProcessor derived; everything here
 * is either a magnitude, a direction used only for binning, or a statistic over
 * values the caller supplied.
 */
class MagQuality
{
public:
    struct Config {
        /// Patches on the unit sphere for magnetometer direction coverage.
        int magPatches = 100;

        /// Patches for gravity direction. Bounded by the width of the per-patch
        /// bitmask in the implementation, so raising it past 32 needs a wider
        /// type there.
        int gravityPatches = 16;

        /// A sample contributes to the dip statistics only when its
        /// acceleration magnitude is within this fraction of one g. Outside it
        /// the accelerometer is measuring motion as well as gravity, so the
        /// gravity direction, and the inclination derived from it, are not
        /// trustworthy.
        ///
        /// This gates the dip metric alone. It is deliberately not a filter on
        /// the magnetometer sample: on the reference capture the correlation
        /// between acceleration excess and field-magnitude error is 0.051, so
        /// dropping those samples costs coverage and buys nothing.
        float accelGate = 0.10f;
    };

    struct Result {
        // --- magnetometer direction coverage ---
        int   patchesSeen = 0;       ///< Occupied patches.
        int   magPatches = 0;        ///< Patches available, for context.
        float coverage = 0.0f;       ///< patchesSeen / magPatches.
        int   patchMin = 0;          ///< Samples in the emptiest occupied patch.
        int   patchMax = 0;          ///< Samples in the fullest patch.

        /// Ratio of the smallest to the largest eigenvalue of the second moment
        /// of the sample directions. One is perfectly uniform; near zero means
        /// the samples lie close to a plane -- the tag spun on a bench without
        /// being tipped, which a patch count alone reports as healthy because
        /// the occupied patches really are spread around.
        float isotropy = 0.0f;

        // --- attitude diversity, needs the accelerometer ---
        /// Mean number of distinct gravity patches visited per occupied
        /// magnetometer patch. One means the tag was never rotated about the
        /// field direction within a patch, so nothing in the data constrains
        /// the off-diagonal soft-iron terms.
        float attitudeDiversity = 0.0f;
        int   attitudeCells = 0;     ///< Distinct (magnetometer, gravity) cells.

        /// Occupied magnetometer patches with at least one gravity
        /// observation: the denominator attitudeDiversity divides by.
        ///
        /// Reported separately because the ratio alone cannot distinguish a
        /// sweep that visited more attitudes from one that occupied fewer
        /// patches -- the mean rises either way, so concentrating the samples
        /// improves it. The cell count is the measure that does not reward
        /// that.
        ///
        /// The ratio's ceiling is also well below the patch count. Field and
        /// gravity are separated by a fixed angle at any site, so within one
        /// magnetometer patch the gravity direction is confined to a small
        /// circle: at gravityPatches = 16 and an inclination of 63.6 degrees
        /// that circle crosses 3.6 patches on average, not 16.
        int   attitudePatches = 0;

        // --- fit residual, from the magnetometer alone ---
        /// Median magnitude of the calibrated samples, which is the field
        /// the fit settled on.
        float field = 0.0f;

        /// RMS distance of the samples from that sphere, as a percentage of
        /// it. Unfloored: a small number means a tight fit, and nothing
        /// clamps it away from zero.
        float fitError = 0.0f;

        /// Robust spread of the same residual, scaled to agree with a
        /// standard deviation, as a percentage of the field. Reported beside
        /// the RMS because they disagree exactly when it matters: a mean
        /// square is set by its largest residual, so a handful of bad
        /// samples inflate it while the MAD stays with the bulk.
        float residualSpread = 0.0f;

        /// 95th percentile of the absolute residual, as a percentage. What
        /// the worst samples are doing, rather than what the typical one is.
        float residualP95 = 0.0f;

        /// Hard iron the fit did not remove, in field units.
        ///
        /// An uncorrected offset d leaves the samples on a sphere centred
        /// off the origin, so a sample in direction u reads |B + d.u| rather
        /// than B: the magnitude residual varies with direction, and
        /// regressing one on the other recovers d. Taken one patch at a
        /// time, a patch contributing its mean residual against its own
        /// centre, so a direction the operator dwelt in weighs no more than
        /// one they passed through.
        ///
        /// Deliberately not the centroid of the sample directions, which is
        /// the obvious construction and does not work: binning by direction
        /// pins each patch's mean direction to its centre, so the very bias
        /// being looked for is averaged away. On a planted 2 unit offset
        /// that reads 0.05 where this reads 1.98.
        ///
        /// Needs coverage to mean anything -- three patches determine the
        /// three components and little more -- so read it with the coverage
        /// figure beside it.
        float residualHardIron = 0.0f;

        // --- dip consistency, needs the accelerometer ---
        /// The angle between the field and gravity belongs to the site, not to
        /// the sample, so its spread is a quality measure the solver does not
        /// optimise. Unlike the fit residual it cannot be improved by
        /// over-fitting, which makes it the one number here that an
        /// over-confident calibration cannot talk its way out of.
        float dipMeanDeg = 0.0f;     ///< Median inclination.
        float dipSpreadDeg = 0.0f;   ///< Robust (MAD-based) spread.
        float dipP95Deg = 0.0f;      ///< 95th percentile absolute deviation.
        int   dipSamples = 0;        ///< Samples that passed the gate.
        int   accelSamples = 0;      ///< Samples offered with an accelerometer.
        bool  haveAccelMetrics = false;

        int   samples = 0;           ///< Samples accepted overall.
    };

    /// Two overloads rather than a defaulted Config argument: naming
    /// Config() as a default inside the class needs MagQuality to be complete,
    /// which it is not yet at that point.
    MagQuality();
    explicit MagQuality(const Config &config);

    /// Drop every sample, keeping the configuration.
    void reset();

    /**
     * @brief Offer one sample.
     *
     * @param mag         Calibrated magnetometer vector. Only its direction is
     *                    used, so the units do not matter, but it must be
     *                    non-zero.
     * @param accel       Accelerometer vector in the caller's units, or nullptr
     *                    when the sample has none.
     * @param dipDegrees  Inclination for this sample as CompassProcessor
     *                    derived it. Ignored when accel is nullptr.
     * @param oneG        Magnitude corresponding to one g in the units of
     *                    accel; 1000 for milli-g. Ignored when accel is
     *                    nullptr.
     *
     * @return false when the sample was rejected outright: a zero-length or
     *         non-finite magnetometer vector. A sample whose accelerometer
     *         fails the gate is still accepted, and still counts for coverage;
     *         only its dip contribution is dropped.
     */
    bool add(const QVector3D &mag, const QVector3D *accel = nullptr,
             float dipDegrees = 0.0f, float oneG = 1.0f);

    Result result() const;

    const Config &config() const { return config_; }

    /**
     * @brief Index of the patch containing a direction, or -1 if unusable.
     *
     * Patch centres come from the spherical Fibonacci lattice, which spaces
     * points near-uniformly without the band structure and hand-written index
     * arithmetic of a latitude-band scheme, and takes the count as a parameter
     * rather than baking it into the arithmetic.
     */
    static int patch(const QVector3D &v, int patchCount);

    /// Centre of a patch, as a unit vector.
    static QVector3D patchCenter(int index, int patchCount);

private:
    Config config_;
    QVector<int> patchCount_;       ///< Samples per magnetometer patch.
    QVector<quint32> gravityMask_;  ///< Bit k set: gravity patch k seen here.

    /// Sum of sample magnitudes in each patch, for the hard-iron regression.
    QVector<double> patchMagnitude_;

    /// Calibrated sample magnitudes, for the residual statistics.
    QVector<float> magnitudes_;
    double moment_[3][3];           ///< Sum of magHat * magHat^T.
    QVector<float> dip_;            ///< Per-sample inclination, degrees.
    int samples_ = 0;
    int accelSamples_ = 0;
};

#endif // MAGQUALITY_H
