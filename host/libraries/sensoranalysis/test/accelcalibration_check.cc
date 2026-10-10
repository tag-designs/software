/**
 * @file accelcalibration_check.cc
 * @brief Offline assertion checks for AccelCalibration.
 *
 * Synthetic sweeps with a planted offset, so the right answer is known before
 * the fit runs. Build with -DBUILD_SENSORANALYSIS_CHECKS=ON.
 */

#include "accelcalibration.h"

#include "directions.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <random>
#include <vector>

namespace {

const double kPi = 3.14159265358979323846;
const float kOneG = 1000.0f;

int failures = 0;

void check(bool condition, const char *what)
{
    std::printf("  %-62s %s\n", what, condition ? "ok" : "FAILED");
    if (!condition) {
        failures++;
    }
}

QVector3D direction(std::mt19937 &rng)
{
    std::uniform_real_distribution<double> uniform(0.0, 1.0);
    const double z = 2.0 * uniform(rng) - 1.0;
    const double lon = 2.0 * kPi * uniform(rng);
    const double r = std::sqrt(std::max(0.0, 1.0 - z * z));
    return QVector3D(static_cast<float>(r * std::cos(lon)),
                     static_cast<float>(r * std::sin(lon)),
                     static_cast<float>(z));
}

/// A reading the tag would produce at rest, with the offset added.
QVector3D resting(const QVector3D &unit, const QVector3D &offset, float noise,
                  std::mt19937 &rng)
{
    std::normal_distribution<float> gauss(0.0f, noise);
    return unit * kOneG + offset
           + QVector3D(gauss(rng), gauss(rng), gauss(rng));
}

void recoversAPlantedOffset()
{
    std::printf("recovers a planted offset\n");
    std::mt19937 rng(1);
    // The offset measured on the reference tag: 76 mg, mostly on z.
    const QVector3D planted(-31.8f, 5.0f, -68.7f);

    AccelCalibration cal;
    for (int i = 0; i < 4000; i++) {
        cal.add(resting(direction(rng), planted, 8.0f, rng), kOneG);
    }
    const AccelCalibration::Result r = cal.result(kOneG);

    check(r.valid, "a full sweep yields a fit");
    check((r.offset - planted).length() < 3.0f,
          "offset recovered to within 3 mg");
    check(std::fabs(r.radius - kOneG) < 5.0f,
          "radius recovered to within 5 mg of one g");
    check(r.patchesSeen == r.patches, "every patch occupied");
    check(r.isotropy > 0.8f, "a full sweep reads as isotropic");
    // Per-axis noise of 8 mg puts the radial scatter at about 8 mg too, so
    // the residual should land near it. It was silently zero until the sum it
    // is built from was corrected.
    check(r.residual > 4.0f && r.residual < 14.0f,
          "residual reports the scatter that was planted");
    std::printf("    offset %+.2f %+.2f %+.2f  radius %.1f  residual %.2f\n",
                r.offset.x(), r.offset.y(), r.offset.z(), r.radius, r.residual);
}

void keepsEveryReading()
{
    std::printf("keeps every reading, however long the sweep\n");
    std::mt19937 rng(2);
    AccelCalibration cal;
    for (int i = 0; i < 50000; i++) {
        cal.add(resting(direction(rng), QVector3D(), 8.0f, rng), kOneG);
    }
    const AccelCalibration::Result r = cal.result(kOneG);
    check(r.samples > 49000, "every accepted reading counts toward the fit");
    check(r.offered == 50000, "every reading was counted as offered");
}

void keepsOccupancyEven()
{
    std::printf("keeps occupancy even when the operator lingers\n");
    std::mt19937 rng(3);
    AccelCalibration cal;

    // A sweep that dwells on one hemisphere: a thousand readings about one
    // direction for every one spread over the sphere. The shared buffer let
    // this crowd the fit; per-patch slots cannot.
    const QVector3D favourite(0.0f, 0.0f, 1.0f);
    for (int i = 0; i < 1000; i++) {
        cal.add(resting(favourite, QVector3D(), 8.0f, rng), kOneG);
    }
    for (int i = 0; i < 2000; i++) {
        cal.add(resting(direction(rng), QVector3D(), 8.0f, rng), kOneG);
    }
    const AccelCalibration::Result r = cal.result(kOneG);
    check(r.patchesSeen > r.patches / 2,
          "the rest of the sphere still occupies most patches");
    // The dwell is a third of the readings but one patch of the weighting,
    // so it cannot pull the centre. Without per-patch weights those thousand
    // readings would outvote every direction they are not in.
    check(r.valid && r.offset.length() < 6.0f,
          "a thousand readings in one direction do not invent an offset");
    std::printf("    %d readings over %d of %d patches, isotropy %.3f, "
                "offset %.2f mg\n",
                r.samples, r.patchesSeen, r.patches, r.isotropy,
                r.offset.length());
}

void rejectsMotion()
{
    std::printf("rejects motion at intake\n");
    std::mt19937 rng(4);
    AccelCalibration cal;

    for (int i = 0; i < 500; i++) {
        cal.add(resting(direction(rng), QVector3D(), 4.0f, rng), kOneG);
    }
    const int heldBefore = cal.result(kOneG).samples;

    // Readings taken mid-swing: well outside the gate in both directions.
    for (int i = 0; i < 500; i++) {
        cal.add(direction(rng) * (kOneG * 1.8f), kOneG);
        cal.add(direction(rng) * (kOneG * 0.2f), kOneG);
    }
    const AccelCalibration::Result r = cal.result(kOneG);
    check(r.samples == heldBefore, "no moving reading reached the buffer");
    check(r.gated >= 1000, "every moving reading was counted as gated");
}

void refusesADegenerateSweep()
{
    std::printf("refuses a sweep the geometry does not support\n");
    std::mt19937 rng(5);

    AccelCalibration spun;
    // Yaw only: the tag turns about the vertical, so gravity never moves in
    // body coordinates. One direction, however many readings.
    for (int i = 0; i < 2000; i++) {
        spun.add(resting(QVector3D(0.0f, 0.0f, 1.0f), QVector3D(20.0f, 0.0f, 0.0f),
                         6.0f, rng), kOneG);
    }
    const AccelCalibration::Result spunResult = spun.result(kOneG);
    check(!spunResult.valid, "a yaw-only sweep is refused");
    check(spunResult.patchesSeen < spun.config().minimumPatches,
          "and is refused on patch count");

    AccelCalibration tipped;
    // A single arc: plenty of patches, but all on one great circle.
    for (int i = 0; i < 4000; i++) {
        std::uniform_real_distribution<double> uniform(0.0, 2.0 * kPi);
        const double a = uniform(rng);
        const QVector3D unit(static_cast<float>(std::cos(a)), 0.0f,
                             static_cast<float>(std::sin(a)));
        tipped.add(resting(unit, QVector3D(20.0f, 0.0f, 0.0f), 6.0f, rng), kOneG);
    }
    const AccelCalibration::Result tippedResult = tipped.result(kOneG);
    check(!tippedResult.valid, "a single-arc sweep is refused");
    check(tippedResult.isotropy < tipped.config().minimumIsotropy,
          "and is refused on evenness, not on patch count");
    std::printf("    arc: %d of %d patches, isotropy %.4f\n",
                tippedResult.patchesSeen, tippedResult.patches,
                tippedResult.isotropy);
}

void doesNotDependOnArrivalOrder()
{
    std::printf("gives the same answer whatever order readings arrive in\n");
    const QVector3D planted(-31.8f, 5.0f, -68.7f);

    // Nothing is displaced now, so any order of any readings must agree:
    // addition is commutative and the fit is a function of the sums.
    std::mt19937 rng(6);
    std::vector<QVector3D> readings;
    for (int i = 0; i < 2000; i++) {
        readings.push_back(resting(direction(rng), planted, 8.0f, rng));
    }

    AccelCalibration forward;
    for (const QVector3D &v : readings) {
        forward.add(v, kOneG);
    }
    AccelCalibration backward;
    for (int i = static_cast<int>(readings.size()) - 1; i >= 0; i--) {
        backward.add(readings[i], kOneG);
    }

    const AccelCalibration::Result a = forward.result(kOneG);
    const AccelCalibration::Result b = backward.result(kOneG);
    check(a.valid && b.valid, "both orders yield a fit");
    check(a.samples == b.samples, "both used the same number of readings");
    check((a.offset - b.offset).length() < 0.01f,
          "offsets agree to rounding");
}

void settlesRatherThanWandering()
{
    std::printf("settles as readings accumulate, rather than wandering\n");
    std::mt19937 rng(7);
    const QVector3D planted(-31.8f, 5.0f, -68.7f);
    AccelCalibration cal;

    std::vector<float> magnitudes;
    for (int i = 0; i < 6000; i++) {
        cal.add(resting(direction(rng), planted, 30.0f, rng), kOneG);
        if (i % 10 == 0) {
            const AccelCalibration::Result r = cal.result(kOneG);
            if (r.valid) {
                magnitudes.push_back(r.offset.length());
            }
        }
    }
    const size_t half = magnitudes.size() / 2;
    float lo = magnitudes[half];
    float hi = magnitudes[half];
    for (size_t i = half; i < magnitudes.size(); i++) {
        lo = std::min(lo, magnitudes[i]);
        hi = std::max(hi, magnitudes[i]);
    }
    // The rule this replaced kept the newest readings per patch, which made
    // the fit a sliding window: on the reference capture it moved 13 mg peak
    // to peak over the second half of a sweep. Accumulating, the estimate
    // only tightens.
    check(hi - lo < 3.0f, "offset moves less than 3 mg over the second half");
    std::printf("    second half: %.2f to %.2f mg, %.2f peak to peak\n",
                lo, hi, hi - lo);
}

void refusesAnEmptyAndAHostileInput()
{
    std::printf("refuses empty and non-finite input\n");
    AccelCalibration cal;
    check(!cal.result(kOneG).valid, "an empty buffer yields no fit");

    const float nan = std::nanf("");
    check(!cal.add(QVector3D(nan, 0.0f, 0.0f), kOneG), "NaN is refused");
    check(!cal.add(QVector3D(0.0f, 0.0f, 0.0f), kOneG), "a zero vector is refused");
    check(!cal.add(QVector3D(0.0f, 0.0f, kOneG), 0.0f), "a zero one-g is refused");
    check(cal.result(kOneG).samples == 0, "and none of them reached the fit");
}

} // namespace

int main()
{
    std::printf("AccelCalibration checks\n\n");
    recoversAPlantedOffset();
    keepsEveryReading();
    keepsOccupancyEven();
    rejectsMotion();
    refusesADegenerateSweep();
    doesNotDependOnArrivalOrder();
    settlesRatherThanWandering();
    refusesAnEmptyAndAHostileInput();
    std::printf("\n%s (%d failures)\n", failures ? "FAILED" : "all ok", failures);
    return failures ? 1 : 0;
}
