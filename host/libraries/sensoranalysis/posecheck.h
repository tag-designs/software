#ifndef POSECHECK_H
#define POSECHECK_H

#include <QVector>
#include <QVector3D>

/*
 * The six orientations an accelerometer calibration asks the operator for.
 *
 * Filling a direction sphere by tumbling works, but it is a poor thing to ask
 * of a person: there is no end state they can see coming and no way to tell
 * which way they have not turned the tag. Every other accelerometer routine
 * asks for the board to be held in a handful of specific orientations
 * instead, and for good reason -- six axis-down poses are a short checklist,
 * each one verifiable, and the geometry they produce is as good as a sweep.
 *
 * This is guidance, not an estimator. AccelCalibration still fits a sphere
 * over everything the operator did, which is more forgiving than solving six
 * exact equations and does not care whether a pose was held squarely. What
 * the poses decide is when to stop asking.
 *
 * The arithmetic behind the convention: with gravity along an axis, the
 * readings at opposite ends of it are r*u + d and -r*u + d, so their midpoint
 * is the offset on that axis exactly, whatever the sensitivity. Three
 * opposite pairs give three independent estimates of it. That is why six is
 * the number, and why they are axis-aligned rather than scattered.
 */
class PoseCheck
{
public:
    /// Indices into the pose list. The order is the order faces are reported
    /// in, and a caller drawing a cube can rely on it.
    ///
    /// Named for the axis the accelerometer reads, which is the face pointing
    /// **up**: an accelerometer at rest measures specific force, so a tag
    /// lying with its +z face up reads +z. Naming them for the face that is
    /// down instead is the obvious mistake and reads backwards on a cube,
    /// because the face being rested on is the one you cannot see.
    enum Pose {
        PlusX = 0,   ///< +x face up; the accelerometer reads +x.
        MinusX,      ///< -x face up.
        PlusY,
        MinusY,
        PlusZ,
        MinusZ,
        PoseCount,
    };

    struct Config {
        /// How far gravity may sit from an axis and still count, in degrees.
        /// Generous on purpose: the fit does not need a square pose, only a
        /// spread of directions, and a tolerance tight enough to be fussy
        /// would turn a fifteen second job into a frustrating one.
        float toleranceDegrees = 20.0f;

        /// How far the magnitude may sit from one g, as a fraction. Rejects a
        /// reading taken while the tag is being moved into position.
        float magnitudeGate = 0.08f;

        /// Consecutive qualifying readings before a pose is counted. Two
        /// seconds at the ten samples a second the calibration stream runs
        /// at, which is both long enough that a pose cannot be claimed in
        /// passing as the tag is turned through it, and short enough to ask
        /// for six times over.
        int holdSamples = 20;
    };

    struct Result {
        QVector<bool> done;        ///< One per pose, in enum order.
        int  completed = 0;        ///< How many are done.
        int  total = PoseCount;
        int  holding = -1;         ///< Pose being held now, or -1.
        int  heldSamples = 0;      ///< Consecutive qualifying readings.
        bool finished = false;     ///< Every pose done.
    };

    PoseCheck();
    explicit PoseCheck(const Config &config);

    void reset();

    /**
     * @brief Offer one accelerometer reading.
     *
     * @param accel  Accelerometer vector in the caller's units.
     * @param oneG   Magnitude corresponding to one g in those units.
     *
     * @return true when this reading completed a pose that was not done
     *         before, so a caller can say so once rather than every sample.
     */
    bool add(const QVector3D &accel, float oneG);

    Result result() const;

    /// Unit vector gravity points along for a pose, in the tag frame.
    static QVector3D direction(int pose);

    const Config &config() const { return config_; }

private:
    Config config_;
    QVector<bool> done_;
    int holding_;
    int held_;
};

#endif // POSECHECK_H
