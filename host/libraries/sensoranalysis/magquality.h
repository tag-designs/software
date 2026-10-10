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
    double moment_[3][3];           ///< Sum of magHat * magHat^T.
    QVector<float> dip_;            ///< Per-sample inclination, degrees.
    int samples_ = 0;
    int accelSamples_ = 0;
};

#endif // MAGQUALITY_H
