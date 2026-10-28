/**
 * @file    calibration_session.h
 * @brief   One live magnetometer and accelerometer calibration: the sample
 *          buffer, the solver, retention, quality and the filtered compass,
 *          Qt-free.
 *
 * @details What qtcalibrate's CompassData did, lifted out of the application
 *          so that a front end without Qt -- the Rust Tag Calibrator's engine
 *          -- runs the same calibration. CompassData is now a Qt adapter over
 *          this class, and results are bit-identical to what it computed
 *          before (qtcalibrate's replay logs, test/calibration_dump.cc).
 *
 *          A session takes calibration samples one at a time:
 *
 *            - the magnetometer reading goes into a 650-slot buffer, the
 *              solver's (magcal/magcal.h), and Freescale's solver refits it;
 *              once the buffer is full, MagRetention chooses which reading to
 *              evict -- or, when it declines, a seeded random slot;
 *            - the accelerometer reading, when there is one, goes to
 *              AccelCalibration, its own population, and is also kept beside
 *              its magnetometer reading so each buffered sample's dip can be
 *              derived for the quality figures.
 *
 *          updateQuality() recomputes MagQuality over the buffer and refreshes
 *          the accelerometer fit; it is the caller's tick, not done per
 *          sample. orientation() solves the live compass from a sample, after
 *          a low-pass filter the session keeps.
 *
 *          Units: magnetometer in µT, accelerometer in mg (kStreamOneG is one
 *          g). Not thread-safe. A session is about 20 KB, most of it the
 *          solver's buffer.
 *
 * @warning The solver refits on every twentieth call to MagCal_Run(), and
 *          counts those calls in a function-level static (magcal.c), not in
 *          the session. Sessions in one process therefore share that count,
 *          and clear() does not reset it. With one session per process, as
 *          qtcalibrate and the Tag Calibrator engine have, this behaves as
 *          qtcalibrate always has. Moving the count into the session would
 *          change when refits happen, so it is a change to measure, not part
 *          of this move.
 *
 * @see     magcal/magcal.h, magretention_core.h, magquality_core.h,
 *          accelcalibration_core.h, compass_core.h
 */
#ifndef SENSORANALYSIS_CORE_CALIBRATION_SESSION_H
#define SENSORANALYSIS_CORE_CALIBRATION_SESSION_H

#include "accelcalibration_core.h"
#include "compass_core.h"
#include "magcal/magcal.h"
#include "magquality_core.h"
#include "magretention_core.h"
#include "vecmath.h"

#include <random>
#include <vector>

namespace calibcore
{

/**
 * @class   CalibrationSession
 * @brief   The live calibration of one tag, from its sample stream.
 */
class CalibrationSession
{
public:
    /// Acceleration magnitude that is one g in calibration stream units: the
    /// stream is milli-g on every current tag. TagInfo carries an
    /// accelconstant that should eventually supply this instead.
    static constexpr float kStreamOneG = 1000.0f;

    /// Buffer slots: the solver's MAGBUFFSIZE.
    static constexpr int kSlots = MAGBUFFSIZE;

    /// Starts empty, as clear() leaves it.
    CalibrationSession();

    /**
     * @brief   Discards every sample, the fit, the accelerometer fit and any
     *          loaded offset, and reseeds the eviction fallback.
     *
     * @post    No calibration (hasCalibration() false); the field reads the
     *          solver's default 50 µT. The live compass filter is not reset.
     */
    void clear();

    /**
     * @brief   Feeds one calibration sample.
     *
     * @param[in] mag       Raw magnetometer vector, µT.
     * @param[in] hasAccel  Whether the sample carried an accelerometer reading.
     * @param[in] accel     The accelerometer reading, mg; ignored when
     *                      hasAccel is false.
     *
     * @return  true when the solver accepted a new fit on this sample, which
     *          changes the constants.
     *
     * @details The magnetometer reading is stored, evicting one when the
     *          buffer is full, and the solver run; the accelerometer reading
     *          is offered to the accelerometer fit, which may refuse it as
     *          motion.
     */
    bool addSample(sensormath::Vec3f mag, bool hasAccel, sensormath::Vec3f accel);

    /// Whether a magnetometer calibration exists (fitted or loaded).
    bool hasCalibration() const;

    /**
     * @brief   Applies the current magnetometer calibration.
     * @param[in] mag  Raw magnetometer vector, µT.
     * @return  A (mag - V), µT. With no calibration, the identity is applied.
     */
    sensormath::Vec3f calibrate(sensormath::Vec3f mag) const;

    /**
     * @brief   Every buffered reading, calibrated, in slot order.
     * @return  Up to kSlots vectors, µT.
     */
    std::vector<sensormath::Vec3f> calibratedPoints() const;

    /**
     * @brief   The current magnetometer constants.
     *
     * @param[out] B  Field magnitude, µT.
     * @param[out] V  Hard-iron offset, µT.
     * @param[out] A  Inverse soft-iron matrix, row-major.
     *
     * @return  hasCalibration(); the outputs are written either way (the
     *          identity and 50 µT before a fit).
     */
    bool constants(float *B, float *V, float (*A)[3]) const;

    /**
     * @brief   Installs magnetometer constants read back from a tag.
     *
     * @details The tag does not store the field magnitude B. It is recomputed
     *          as the mean calibrated magnitude of the buffered readings, and
     *          left as it was when the buffer is empty.
     *
     * @param[in] V  Hard-iron offset, µT.
     * @param[in] A  Inverse soft-iron matrix, row-major.
     *
     * @post    hasCalibration() is true. The solver may replace these on a
     *          later sample, as with any fit.
     */
    void setConstants(const float *V, const float (*A)[3]);

    /// The field magnitude B, µT: the fit's, recomputed on load, or 50 before.
    float field() const;

    /**
     * @brief   Installs an accelerometer offset read back from a tag.
     *
     * @param[in] offset  Zero-g offset, mg.
     *
     * @details Marks the accelerometer result valid with no fit statistics.
     *          It stands until the live population yields a valid fit, or the
     *          session is cleared.
     */
    void setAccelOffset(sensormath::Vec3f offset);

    /**
     * @brief   Recomputes the quality figures over the buffer, and the
     *          accelerometer fit.
     *
     * @details Each buffered reading is calibrated and offered to MagQuality,
     *          with its paired accelerometer reading and the dip the compass
     *          solve derives from the two after removing the accelerometer
     *          offset. A loaded accelerometer offset stands until the live
     *          fit is valid.
     */
    void updateQuality();

    /// The quality figures as of the last updateQuality().
    const MagQuality::Result &quality() const { return metrics_; }

    /// The accelerometer offset in force: the live fit as of the last
    /// updateQuality(), or one loaded with setAccelOffset().
    const AccelCalibration::Result &accelOffset() const { return gravity_; }

    /// Evictions since clear().
    int evictionCount() const { return evictions_; }
    /// Of those, decided as the least informative sample.
    int leverageEvictionCount() const { return leverageEvictions_; }
    /// Of those, decided as an outlier.
    int outlierEvictionCount() const { return outlierEvictions_; }

    /**
     * @brief   Solves the live compass from one sample.
     *
     * @details Calibrates the magnetometer reading, removes the accelerometer
     *          offset when one is valid, low-pass filters both vectors
     *          (0.4 old, 0.6 new) and solves the orientation from the filtered
     *          pair. The filter state persists across calls and clear().
     *
     * @param[in]  mag     Raw magnetometer vector, µT.
     * @param[in]  accel   Raw accelerometer vector, mg.
     * @param[out] result  The orientation, dip and field.
     *
     * @return  false when the filtered vectors admit no orientation (a zero
     *          or parallel pair); result is then unspecified.
     */
    bool orientation(sensormath::Vec3f mag, sensormath::Vec3f accel, compasscore::Orientation &result);

    /// The solver's state, read-only: the buffer, its validity flags, the
    /// fit and its error.
    const MagCalibration_t &solver() const { return magcal_; }

private:
    /// The current calibration as compass constants (no accelerometer offset).
    compasscore::CalibrationConstants magnetometerConstants() const;
    /// Slot @p i's reading, uncalibrated; zero outside the buffer.
    sensormath::Vec3f slot(int i) const;
    /// Stores a reading, evicting one when the buffer is full.
    void store(sensormath::Vec3f mag, bool hasAccel, sensormath::Vec3f accel);
    /// The slot MagRetention chooses to evict, or -1 when it declines.
    int chooseEviction();

    MagCalibration_t magcal_;                ///< The solver: buffer, fit, scratch.

    /// Accelerometer reading paired with each slot, indexed as magcal_.BpFast.
    sensormath::Vec3f accelBuffer_[kSlots];
    bool accelBufferValid_[kSlots] = {false}; ///< accelBuffer_[i] holds a reading.

    MagQuality quality_;                     ///< Recomputed by updateQuality().
    MagQuality::Result metrics_;             ///< Its last result.

    AccelCalibration accelCal_;              ///< The accelerometer's own population.
    AccelCalibration::Result gravity_;       ///< The offset in force.
    bool gravityLoaded_ = false;             ///< gravity_ was loaded, not fitted.

    MagRetention retention_;                 ///< Leverage and outlier eviction.

    /// Additions so far, for probation: a slot is exempt from eviction for
    /// kProbationAdds additions after it is filled, so a transient bad
    /// calibration cannot immediately purge the evidence that would correct
    /// it.
    int addCounter_ = 0;
    int slotFilledAt_[kSlots] = {0};         ///< addCounter_ when each slot was filled.

    /// The fallback when MagRetention declines. Seeded from a constant, and
    /// reseeded on every clear(): the randomness only avoids a systematic
    /// bias in which of two equally redundant samples is dropped, which a
    /// fixed stream does as well as an unpredictable one, and it makes a
    /// replay of one capture reproduce exactly. std::mt19937 rather than
    /// rand() because rand()'s sequence differs between platforms.
    std::mt19937 discardRng_;

    int evictions_ = 0;                      ///< Since clear().
    int leverageEvictions_ = 0;              ///< Of those, by leverage.
    int outlierEvictions_ = 0;               ///< Of those, as outliers.

    sensormath::Vec3f accFilt_;              ///< Live compass filter, accelerometer.
    sensormath::Vec3f magFilt_;              ///< Live compass filter, magnetometer.
};

} // namespace calibcore

#endif // SENSORANALYSIS_CORE_CALIBRATION_SESSION_H
