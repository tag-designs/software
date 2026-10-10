/**
 * @file magquality_check.cc
 * @brief Offline assertion checks for MagQuality.
 *
 * Every case is synthetic data with an answer known in advance, so this needs
 * no tag, no capture and no calibration. Build with
 * -DBUILD_SENSORANALYSIS_CHECKS=ON and run it by hand; see test/README.md.
 */

#include "magquality.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <random>
#include <vector>

namespace {

const double kPi = 3.14159265358979323846;

int failures = 0;

void check(bool condition, const char *what)
{
    std::printf("  %-58s %s\n", what, condition ? "ok" : "FAILED");
    if (!condition) {
        failures++;
    }
}

/// Directions spread over the sphere, deterministic for a given seed.
QVector3D randomDirection(std::mt19937 &rng)
{
    std::uniform_real_distribution<double> uniform(0.0, 1.0);
    const double z = 2.0 * uniform(rng) - 1.0;
    const double lon = 2.0 * kPi * uniform(rng);
    const double r = std::sqrt(std::max(0.0, 1.0 - z * z));
    return QVector3D(static_cast<float>(r * std::cos(lon)),
                     static_cast<float>(r * std::sin(lon)),
                     static_cast<float>(z));
}

void checkPatchLattice()
{
    std::printf("patch lattice\n");

    bool unitLength = true;
    bool roundTrips = true;
    const int n = 100;
    for (int i = 0; i < n; i++) {
        const QVector3D centre = MagQuality::patchCenter(i, n);
        if (std::fabs(centre.length() - 1.0f) > 1e-4f) {
            unitLength = false;
        }
        // The nearest centre to a centre is itself, or the lattice is not a
        // partition and every count below is meaningless.
        if (MagQuality::patch(centre, n) != i) {
            roundTrips = false;
        }
    }
    check(unitLength, "every patch centre is a unit vector");
    check(roundTrips, "each centre falls in its own patch");

    check(MagQuality::patch(QVector3D(0, 0, 0), n) == -1,
          "a zero vector has no patch");

    // Near-uniformity: with an even spread no patch should be wildly larger
    // than another. Count a dense sample set and compare extremes.
    std::mt19937 rng(1234);
    std::vector<int> counts(n, 0);
    const int samples = 200000;
    for (int i = 0; i < samples; i++) {
        const int index = MagQuality::patch(randomDirection(rng), n);
        assert(index >= 0 && index < n);
        counts[index]++;
    }
    int lo = samples;
    int hi = 0;
    for (int i = 0; i < n; i++) {
        lo = std::min(lo, counts[i]);
        hi = std::max(hi, counts[i]);
    }
    const double ratio = static_cast<double>(hi) / static_cast<double>(lo);
    std::printf("      busiest/emptiest patch over %d samples: %.2f\n",
                samples, ratio);
    check(ratio < 1.5, "patches receive within 1.5x of each other");
}

void checkCoverageAndIsotropy()
{
    std::printf("coverage and isotropy\n");

    std::mt19937 rng(7);
    MagQuality spread;
    for (int i = 0; i < 5000; i++) {
        spread.add(randomDirection(rng) * 46.0f);
    }
    const MagQuality::Result full = spread.result();
    std::printf("      uniform: coverage %.2f, isotropy %.3f\n",
                full.coverage, full.isotropy);
    check(full.patchesSeen == full.magPatches, "uniform data occupies every patch");
    check(full.isotropy > 0.9f, "uniform data is isotropic");
    check(full.samples == 5000, "every sample was accepted");

    // The turntable failure: a tag spun flat covers a ring. A patch count sees
    // plenty of distinct patches, so only the isotropy catches it.
    MagQuality ring;
    for (int i = 0; i < 2000; i++) {
        const double lon = 2.0 * kPi * i / 2000.0;
        ring.add(QVector3D(static_cast<float>(46.0 * std::cos(lon)),
                           static_cast<float>(46.0 * std::sin(lon)),
                           0.0f));
    }
    const MagQuality::Result planar = ring.result();
    std::printf("      coplanar: coverage %.2f, isotropy %.3f\n",
                planar.coverage, planar.isotropy);
    check(planar.isotropy < 0.01f, "coplanar data is not isotropic");
    check(planar.patchesSeen > 10,
          "coplanar data still occupies many patches, so the count alone misses it");

    MagQuality empty;
    const MagQuality::Result none = empty.result();
    check(none.samples == 0 && none.patchesSeen == 0 && !none.haveAccelMetrics,
          "an empty context reports nothing rather than misbehaving");
}

void checkDipStatistics()
{
    std::printf("dip consistency\n");

    std::mt19937 rng(11);
    const QVector3D gravity(0.0f, 0.0f, 1000.0f);

    // A constant inclination must come back with no spread at all.
    MagQuality clean;
    for (int i = 0; i < 1000; i++) {
        clean.add(randomDirection(rng) * 46.0f, &gravity, 67.0f, 1000.0f);
    }
    const MagQuality::Result steady = clean.result();
    std::printf("      constant 67 deg: mean %.3f, spread %.4f, n %d\n",
                steady.dipMeanDeg, steady.dipSpreadDeg, steady.dipSamples);
    check(std::fabs(steady.dipMeanDeg - 67.0f) < 1e-3f, "constant dip is recovered");
    check(steady.dipSpreadDeg < 1e-3f, "constant dip has no spread");
    check(steady.dipSamples == 1000, "every in-gate sample counted");

    // Known noise must come back as roughly that much spread, since the MAD is
    // scaled to agree with a standard deviation on normal data.
    std::normal_distribution<double> noise(67.0, 2.0);
    MagQuality noisy;
    for (int i = 0; i < 20000; i++) {
        noisy.add(randomDirection(rng) * 46.0f, &gravity,
                  static_cast<float>(noise(rng)), 1000.0f);
    }
    const MagQuality::Result scattered = noisy.result();
    std::printf("      2 deg noise: mean %.3f, spread %.3f\n",
                scattered.dipMeanDeg, scattered.dipSpreadDeg);
    check(std::fabs(scattered.dipSpreadDeg - 2.0f) < 0.2f,
          "the robust spread recovers a known 2 degree scatter");
}

void checkAccelerationGate()
{
    std::printf("acceleration gate\n");

    std::mt19937 rng(13);
    const QVector3D still(0.0f, 0.0f, 1000.0f);
    const QVector3D thrown(0.0f, 0.0f, 1600.0f);   // 1.6 g, well outside

    MagQuality q;
    for (int i = 0; i < 500; i++) {
        q.add(randomDirection(rng) * 46.0f, &still, 67.0f, 1000.0f);
        // A wild dip value, which must be excluded from the statistics but
        // must NOT cost us the sample's coverage contribution.
        q.add(randomDirection(rng) * 46.0f, &thrown, -20.0f, 1000.0f);
    }
    const MagQuality::Result r = q.result();
    std::printf("      samples %d, with accel %d, in gate %d, dip %.3f\n",
                r.samples, r.accelSamples, r.dipSamples, r.dipMeanDeg);
    check(r.samples == 1000, "every sample counts for coverage");
    check(r.accelSamples == 1000, "every sample offered an accelerometer");
    check(r.dipSamples == 500, "only the still samples reach the dip statistics");
    check(std::fabs(r.dipMeanDeg - 67.0f) < 1e-3f,
          "the thrown samples did not drag the inclination");
}

void checkAttitudeDiversity()
{
    std::printf("attitude diversity\n");

    // One magnetometer direction held while gravity sweeps: the tag was
    // rotated about the field, which is what frees the off-diagonal terms.
    const QVector3D fixedMag(46.0f, 0.0f, 0.0f);
    MagQuality varied;
    for (int i = 0; i < 400; i++) {
        const double lon = 2.0 * kPi * i / 400.0;
        const QVector3D gravity(static_cast<float>(1000.0 * std::cos(lon)),
                                static_cast<float>(1000.0 * std::sin(lon)),
                                0.0f);
        varied.add(fixedMag, &gravity, 67.0f, 1000.0f);
    }
    const MagQuality::Result spun = varied.result();
    std::printf("      swept gravity: diversity %.2f over %d cells\n",
                spun.attitudeDiversity, spun.attitudeCells);
    check(spun.attitudeDiversity > 1.5f,
          "sweeping gravity in one patch raises diversity above one");

    // The same magnetometer direction at one attitude throughout.
    const QVector3D oneGravity(0.0f, 0.0f, 1000.0f);
    MagQuality frozen;
    for (int i = 0; i < 400; i++) {
        frozen.add(fixedMag, &oneGravity, 67.0f, 1000.0f);
    }
    const MagQuality::Result stuck = frozen.result();
    std::printf("      fixed gravity: diversity %.2f over %d cells\n",
                stuck.attitudeDiversity, stuck.attitudeCells);
    check(std::fabs(stuck.attitudeDiversity - 1.0f) < 1e-6f,
          "one attitude throughout gives diversity exactly one");

    // Without an accelerometer there is nothing to say.
    MagQuality magOnly;
    std::mt19937 rng(17);
    for (int i = 0; i < 200; i++) {
        magOnly.add(randomDirection(rng) * 46.0f);
    }
    const MagQuality::Result bare = magOnly.result();
    check(bare.accelSamples == 0 && bare.attitudeCells == 0
              && !bare.haveAccelMetrics,
          "no accelerometer means no accelerometer metrics");
    check(bare.patchesSeen > 0, "coverage still works without an accelerometer");
}

} // namespace

void reportsFitResidual()
{
    std::printf("fit residual\n");
    std::mt19937 rng(31);
    const float B = 45.0f;

    // A clean sphere: everything on it, nothing off it.
    MagQuality clean;
    for (int i = 0; i < 4000; i++) {
        clean.add(randomDirection(rng) * B);
    }
    MagQuality::Result r = clean.result();
    check(std::fabs(r.field - B) < 0.01f, "the field is the sphere's radius");
    check(r.fitError < 0.01f, "a sphere has no fit error");
    check(r.residualHardIron < 0.5f, "and no residual hard iron");
    std::printf("    clean: field %.2f, fit error %.4f%%, hard iron %.3f\n",
                r.field, r.fitError, r.residualHardIron);

    // One percent of scatter in the radius comes back as one percent.
    MagQuality noisy;
    std::normal_distribution<float> gauss(0.0f, 0.01f * B);
    for (int i = 0; i < 4000; i++) {
        noisy.add(randomDirection(rng) * (B + gauss(rng)));
    }
    r = noisy.result();
    check(r.fitError > 0.8f && r.fitError < 1.2f,
          "one percent of radial scatter reads as about one percent");
    check(r.residualSpread > 0.8f && r.residualSpread < 1.2f,
          "and the robust spread agrees with it on clean noise");

    // Where they part company: a few gross samples set the mean square and
    // not the median, which is the whole reason both are reported.
    MagQuality spoiled;
    for (int i = 0; i < 4000; i++) {
        spoiled.add(randomDirection(rng) * (B + gauss(rng)));
    }
    for (int i = 0; i < 40; i++) {
        spoiled.add(randomDirection(rng) * (B * 1.5f));
    }
    r = spoiled.result();
    check(r.fitError > 2.0f * r.residualSpread,
          "one percent of gross samples inflate the RMS, not the MAD");
    check(r.residualP95 > r.residualSpread,
          "and the 95th percentile sees them too");
    std::printf("    spoiled: fit error %.2f%%, robust spread %.2f%%, "
                "p95 %.2f%%\n", r.fitError, r.residualSpread, r.residualP95);
}

void findsResidualHardIron()
{
    std::printf("residual hard iron\n");
    std::mt19937 rng(32);
    const float B = 45.0f;
    const QVector3D leftover(0.0f, 0.0f, 2.0f);

    MagQuality offset;
    for (int i = 0; i < 6000; i++) {
        offset.add(randomDirection(rng) * B + leftover);
    }
    const MagQuality::Result r = offset.result();
    // Regressing the magnitude residual on direction recovers the offset
    // itself, not a fraction of it.
    check(r.residualHardIron > 1.8f && r.residualHardIron < 2.2f,
          "a 2 unit leftover offset reads as about 2 units");
    std::printf("    2.0 unit offset reads as %.2f\n", r.residualHardIron);

    // Dwelling in one direction is not an offset, which is what the
    // per-patch vote is for.
    MagQuality dwell;
    for (int i = 0; i < 6000; i++) {
        dwell.add(randomDirection(rng) * B);
    }
    for (int i = 0; i < 6000; i++) {
        dwell.add(QVector3D(0.0f, 0.0f, 1.0f) * B);
    }
    check(dwell.result().residualHardIron < 0.2f,
          "six thousand readings in one direction do not invent one");
    std::printf("    dwell of 6000 reads as %.2f\n",
                dwell.result().residualHardIron);

    // The harder case: a real offset, with a dwell on top of it. The offset
    // must still come back, which per-sample averaging cannot manage.
    MagQuality both;
    for (int i = 0; i < 6000; i++) {
        both.add(randomDirection(rng) * B + leftover);
    }
    for (int i = 0; i < 6000; i++) {
        both.add(QVector3D(0.0f, 0.0f, 1.0f) * B + leftover);
    }
    const float withDwell = both.result().residualHardIron;
    check(withDwell > 1.8f && withDwell < 2.2f,
          "and a dwell on top of a real offset does not hide it");
    std::printf("    2.0 offset plus a 6000 dwell reads as %.2f\n", withDwell);
}

int main()
{
    std::printf("MagQuality offline checks\n\n");
    checkPatchLattice();
    std::printf("\n");
    checkCoverageAndIsotropy();
    std::printf("\n");
    checkDipStatistics();
    std::printf("\n");
    checkAccelerationGate();
    std::printf("\n");
    checkAttitudeDiversity();
    std::printf("\n");
    reportsFitResidual();
    std::printf("\n");
    findsResidualHardIron();
    std::printf("\n");

    if (failures == 0) {
        std::printf("all checks passed\n");
        return 0;
    }
    std::printf("%d check(s) FAILED\n", failures);
    return 1;
}
