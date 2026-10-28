/**
 * @file    calibration_session.cpp
 * @brief   The live calibration of one tag, Qt-free.
 *
 * @details Moved from qtcalibrate's compassdata.cpp, expression for
 *          expression, onto sensormath types and an owned solver state; see
 *          calibration_session.h.
 */
#include "calibration_session.h"

#include <cstring>

using sensormath::Vec3f;

namespace {

/// Additions for which a freshly stored sample is exempt from eviction. One
/// solver cycle: MagCal_Run() refits every twentieth sample, so a sample
/// survives long enough to have influenced a fit before it can be judged by
/// one.
const int kProbationAdds = 20;

/// Seed of the eviction fallback, set again on every clear().
const unsigned int kDiscardSeed = 20261010u;

} // namespace

namespace calibcore {

CalibrationSession::CalibrationSession()
    : discardRng_(kDiscardSeed)
{
    clear();
}

void CalibrationSession::clear()
{
    std::memset(static_cast<void *>(&magcal_), 0, sizeof(magcal_));
    for (int i = 0; i < kSlots; i++) {
        accelBufferValid_[i] = false;
        accelBuffer_[i] = Vec3f{};
        slotFilledAt_[i] = 0;
    }
    addCounter_ = 0;
    discardRng_.seed(kDiscardSeed);
    accelCal_.reset();
    gravity_ = AccelCalibration::Result();
    gravityLoaded_ = false;
    evictions_ = 0;
    leverageEvictions_ = 0;
    outlierEvictions_ = 0;
    magcal_.invW[0][0] = 1.0f;
    magcal_.invW[1][1] = 1.0f;
    magcal_.invW[2][2] = 1.0f;
    magcal_.FitError = 100.0f;
    magcal_.FitErrorAge = 100.0f;
    magcal_.B = 50.0f;
}

compasscore::CalibrationConstants CalibrationSession::magnetometerConstants() const
{
    compasscore::CalibrationConstants constants;
    constants.hardIron = Vec3f{magcal_.V[0], magcal_.V[1], magcal_.V[2]};
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            constants.softIron[i][j] = magcal_.invW[i][j];
        }
    }
    return constants;
}

Vec3f CalibrationSession::calibrate(Vec3f mag) const
{
    return compasscore::applyMagnetometer(magnetometerConstants(), mag);
}

Vec3f CalibrationSession::slot(int i) const
{
    if (i < 0 || i >= kSlots) {
        return Vec3f{};
    }
    return Vec3f{magcal_.BpFast[0][i], magcal_.BpFast[1][i], magcal_.BpFast[2][i]};
}

bool CalibrationSession::hasCalibration() const
{
    return magcal_.ValidMagCal != 0;
}

std::vector<Vec3f> CalibrationSession::calibratedPoints() const
{
    std::vector<Vec3f> points;
    for (int i = 0; i < kSlots; i++) {
        if (magcal_.valid[i]) {
            points.push_back(calibrate(slot(i)));
        }
    }
    return points;
}

bool CalibrationSession::constants(float *B, float *V, float (*A)[3]) const
{
    *B = magcal_.B;
    for (int i = 0; i < 3; i++) {
        V[i] = magcal_.V[i];
        for (int j = 0; j < 3; j++) {
            A[i][j] = magcal_.invW[i][j];
        }
    }
    return magcal_.ValidMagCal;
}

void CalibrationSession::setConstants(const float *V, const float (*A)[3])
{
    for (int i = 0; i < 3; i++) {
        magcal_.V[i] = V[i];
        for (int j = 0; j < 3; j++) {
            magcal_.invW[i][j] = A[i][j];
        }
    }
    magcal_.ValidMagCal = 4;

    // B is not stored on the tag; recover it from the buffer if there is one.
    const compasscore::CalibrationConstants constants = magnetometerConstants();
    double sum = 0.0;
    int count = 0;
    for (int i = 0; i < kSlots; i++) {
        if (magcal_.valid[i]) {
            sum += sensormath::length(compasscore::applyMagnetometer(constants, slot(i)));
            count++;
        }
    }
    if (count > 0) {
        magcal_.B = sum / count;
    }
}

float CalibrationSession::field() const
{
    return magcal_.B;
}

void CalibrationSession::setAccelOffset(Vec3f offset)
{
    gravity_ = AccelCalibration::Result();
    gravity_.valid = true;
    gravity_.offset = offset;
    gravityLoaded_ = true;
}

bool CalibrationSession::orientation(Vec3f mag, Vec3f accel, compasscore::Orientation &result)
{
    const float alpha = 0.4f; // filter coefficient

    mag = calibrate(mag);

    // The live compass is filtered before solving; log viewers solve
    // unfiltered samples.
    if (gravity_.valid) {
        accel = accel - gravity_.offset;
    }
    // The new sample's weight is 1 - alpha computed in double and rounded to
    // float, as the Qt code it replaced did, so results stay bit-identical.
    const float beta = static_cast<float>(1.0 - alpha);
    accFilt_ = alpha * accFilt_ + beta * accel;
    magFilt_ = alpha * magFilt_ + beta * mag;

    return compasscore::solveOrientation(magFilt_, accFilt_, nullptr, result);
}

void CalibrationSession::updateQuality()
{
    quality_.reset();

    // The accelerometer fit comes from its own population, which has been
    // accumulating at intake rather than waiting for this tick. An offset
    // loaded from the tag stands until the live population yields a valid fit
    // of its own.
    const AccelCalibration::Result fit = accelCal_.result(kStreamOneG);
    if (fit.valid || !gravityLoaded_) {
        gravity_ = fit;
        gravityLoaded_ = false;
    }

    // The inclination of a sample is the compass solve's to compute: the
    // magnetometer and the accelerometer do not share an axis convention, and
    // that conversion lives in one place. See docs/shared/sensor-axes.md.
    // MagQuality only aggregates what it is handed.
    for (int i = 0; i < kSlots; i++) {
        if (!magcal_.valid[i]) {
            continue;
        }
        const Vec3f point = calibrate(slot(i));
        if (accelBufferValid_[i]) {
            // The zero-g offset tilts the apparent gravity by an amount that
            // varies with orientation, so it shows up as dip scatter. Removing
            // it is worth about a third of that spread.
            const Vec3f accel = gravity_.valid ? accelBuffer_[i] - gravity_.offset
                                               : accelBuffer_[i];
            compasscore::Orientation derived;
            if (compasscore::solveOrientation(point, accel, nullptr, derived)) {
                quality_.add(point, &accelBuffer_[i], derived.dip, kStreamOneG);
            } else {
                quality_.add(point);
            }
        } else {
            quality_.add(point);
        }
    }
    metrics_ = quality_.result();
}

/**
 * @details Samples are handed over calibrated. Leverage is unchanged by that:
 *          an affine change of coordinates maps the ten quadratic basis terms
 *          onto linear combinations of themselves, so the design matrix is
 *          reparametrised and leverage is invariant. The residual is not
 *          invariant, and needs the calibrated magnitudes to mean anything,
 *          which is why the outlier rule is only armed once a calibration
 *          exists -- before that, "wrong magnitude" has no definition and the
 *          hard iron offset alone would make honest samples look wild.
 */
int CalibrationSession::chooseEviction()
{
    std::vector<Vec3f> samples;
    std::vector<bool> probation;
    std::vector<int> slotOf;
    samples.reserve(kSlots);
    probation.reserve(kSlots);
    slotOf.reserve(kSlots);

    for (int i = 0; i < kSlots; i++) {
        if (!magcal_.valid[i]) {
            continue;
        }
        samples.push_back(calibrate(slot(i)));
        probation.push_back((addCounter_ - slotFilledAt_[i]) < kProbationAdds);
        slotOf.push_back(i);
    }

    MagRetention::Config cfg = retention_.config();
    cfg.rejectOutliers = (magcal_.ValidMagCal != 0);
    const MagRetention::Choice choice = MagRetention(cfg).choose(samples, probation);

    if (choice.reason == MagRetention::Reason::None
        || choice.index < 0 || choice.index >= static_cast<int>(slotOf.size())) {
        return -1;
    }
    if (choice.reason == MagRetention::Reason::Outlier) {
        outlierEvictions_++;
    } else {
        leverageEvictions_++;
    }
    return slotOf[choice.index];
}

void CalibrationSession::store(Vec3f mag, bool hasAccel, Vec3f accel)
{
    int i;

    // First look for an unused slot.
    for (i = 0; i < kSlots; i++) {
        if (!magcal_.valid[i]) {
            break;
        }
    }
    // If the buffer is full, one reading must go. Throwing away the wrong one
    // could prevent the buffer from ever covering the whole sphere, and being
    // wrong about which readings are bad could feed back: bad data leads to
    // wrong decisions, which lead to worse data. MagRetention's floor and
    // probation guard against that; when it declines, a seeded random slot.
    if (i >= kSlots) {
        i = chooseEviction();
        if (i < 0 || i >= kSlots) {
            i = static_cast<int>(discardRng_() % kSlots);
        }
        evictions_++;
    }
    addCounter_++;
    slotFilledAt_[i] = addCounter_;
    magcal_.BpFast[0][i] = mag.x;
    magcal_.BpFast[1][i] = mag.y;
    magcal_.BpFast[2][i] = mag.z;
    magcal_.valid[i] = 1;

    // Whichever slot was chosen, its old accelerometer reading goes with its
    // old magnetometer reading.
    accelBuffer_[i] = accel;
    accelBufferValid_[i] = hasAccel;
}

bool CalibrationSession::addSample(Vec3f mag, bool hasAccel, Vec3f accel)
{
    // Two independent intakes. The magnetometer takes every sample; the
    // accelerometer takes the ones that are gravity rather than motion, and
    // keeps them by direction. Neither decides anything for the other.
    if (hasAccel) {
        accelCal_.add(accel, kStreamOneG);
    }
    store(mag, hasAccel, accel);
    return MagCal_Run(&magcal_) != 0;
}

} // namespace calibcore
