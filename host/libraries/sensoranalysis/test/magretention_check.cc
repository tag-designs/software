/**
 * @file magretention_check.cc
 * @brief Offline assertion checks for MagRetention.
 *
 * Synthetic data with answers known in advance; no tag, no capture. Build with
 * -DBUILD_SENSORANALYSIS_CHECKS=ON. See test/README.md.
 */

#include "magretention.h"
#include "magquality.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <random>
#include <vector>

namespace {

const double kPi = 3.14159265358979323846;
const float kField = 46.0f;

int failures = 0;

void check(bool condition, const char *what)
{
    std::printf("  %-62s %s\n", what, condition ? "ok" : "FAILED");
    if (!condition) {
        failures++;
    }
}

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

/**
 * @brief Random directions at a noisy radius.
 *
 * @details The noise is not decoration. A set of samples at exactly one radius
 *          makes x^2 + y^2 + z^2 constant, which is a linear dependency among
 *          the quadratic columns and the constant column, so the design matrix
 *          is rank deficient and the ten-parameter fit is not determined. Real
 *          captures always have scatter; synthetic data without it tests a
 *          case that cannot occur and fails for reasons of its own.
 */
QVector<QVector3D> sphere(int n, std::mt19937 &rng, float radius = kField,
                          float noise = 0.5f)
{
    std::normal_distribution<float> jitter(0.0f, noise);
    QVector<QVector3D> out;
    out.reserve(n);
    for (int i = 0; i < n; i++) {
        out.append(randomDirection(rng) * (radius + jitter(rng)));
    }
    return out;
}

void checkLeverageIdentity()
{
    std::printf("leverage\n");

    std::mt19937 rng(3);
    const QVector<QVector3D> samples = sphere(650, rng);
    MagRetention retention;
    const QVector<float> h = retention.leverages(samples);

    check(!h.isEmpty(), "leverage is defined for well spread samples");
    double sum = 0.0;
    for (int i = 0; i < h.size(); i++) {
        sum += h.at(i);
    }
    std::printf("      sum of leverages %.9f over %d samples\n",
                sum, static_cast<int>(h.size()));
    // The hat matrix has trace p, so the leverages sum to p for any data that
    // determines the fit. If this holds, the computation is right.
    check(std::fabs(sum - MagRetention::kParameters) < 1e-4,
          "leverages sum to the parameter count");

    bool inRange = true;
    for (int i = 0; i < h.size(); i++) {
        if (h.at(i) < 0.0f || h.at(i) > 1.0f) {
            inRange = false;
        }
    }
    check(inRange, "every leverage lies between zero and one");
}

void checkRankDeficiency()
{
    std::printf("rank deficiency\n");

    MagRetention retention;

    QVector<QVector3D> ring;
    for (int i = 0; i < 300; i++) {
        const double lon = 2.0 * kPi * i / 300.0;
        ring.append(QVector3D(static_cast<float>(kField * std::cos(lon)),
                              static_cast<float>(kField * std::sin(lon)),
                              0.0f));
    }
    check(retention.leverages(ring).isEmpty(),
          "coplanar samples are refused rather than answered");

    // A perfect sphere is rank deficient too, for a different reason: it makes
    // the quadratic columns sum to a constant.
    std::mt19937 exact(23);
    check(retention.leverages(sphere(300, exact, kField, 0.0f)).isEmpty(),
          "samples at one exact radius are refused");
    check(retention.choose(ring).reason == MagRetention::Reason::None,
          "and no eviction is chosen from them");

    std::mt19937 rng(5);
    const QVector<QVector3D> few = sphere(8, rng);
    check(retention.leverages(few).isEmpty(),
          "fewer samples than parameters are refused");
}

void checkRedundancy()
{
    std::printf("redundancy\n");

    std::mt19937 rng(7);
    const QVector<QVector3D> base = sphere(300, rng);
    const QVector3D crowded = base.at(10);

    MagRetention retention;

    // Redundancy lowers a sample's leverage: each near-duplicate added takes
    // something away from what the original uniquely contributes. This is the
    // property that makes leverage the right answer to the question the
    // inherited nearest-pair scan was asking.
    //
    // Note what is NOT asserted. A cloned sample is not necessarily below the
    // mean leverage, and a small crowd is not necessarily where the minimum
    // falls: leverage is a property of the whole design, so a sample sitting
    // in a naturally dense region of the sphere can contribute less than a
    // duplicated one elsewhere. That is a real difference from counting near
    // neighbours, and it is the behaviour we want rather than a defect.
    double previous = 0.0;
    bool monotone = true;
    for (int clones = 0; clones <= 20; clones += 5) {
        QVector<QVector3D> samples = base;
        for (int i = 0; i < clones; i++) {
            samples.append(crowded * (1.0f + 1e-6f * i));
        }
        const QVector<float> h = retention.leverages(samples);
        const double now = h.at(10);
        std::printf("      %2d clones: leverage of the original %.5f\n",
                    clones, now);
        if (clones > 0 && !(now < previous)) {
            monotone = false;
        }
        previous = now;
    }
    check(monotone, "every near-duplicate added lowers the original's leverage");
}

void checkOutlier()
{
    std::printf("outliers\n");

    std::mt19937 rng(11);
    QVector<QVector3D> samples = sphere(300, rng);

    // A sample with a badly wrong magnitude, placed in a direction that
    // already has neighbours so the coverage floor does not shield it.
    const QVector3D direction = samples.at(20).normalized();
    samples.append(direction * (kField * 1.8f));
    const int planted = 300;

    MagRetention retention;
    const MagRetention::Choice choice = retention.choose(samples);
    std::printf("      evicted index %d, reason %s, score %.2f\n", choice.index,
                choice.reason == MagRetention::Reason::Outlier ? "outlier"
                                                               : "leverage",
                choice.score);
    check(choice.index == planted, "the planted outlier is the one evicted");
    check(choice.reason == MagRetention::Reason::Outlier,
          "and it is evicted as an outlier, not as uninformative");

    // With the outlier rule off, the same set should fall back to leverage and
    // leave the outlier alone -- it is a high-leverage sample, not a redundant
    // one, which is exactly why leverage alone never removes it.
    MagRetention::Config quiet;
    quiet.rejectOutliers = false;
    const MagRetention::Choice byLeverage = MagRetention(quiet).choose(samples);
    check(byLeverage.index != planted,
          "with the outlier rule off, leverage alone does not remove it");

    // The point of using a studentized residual rather than Cook's distance.
    // Cook's is residual times leverage, so a sample that is merely unusual
    // in direction -- high leverage, honest magnitude -- scored as an outlier
    // precisely because it was informative. Here it must survive: being the
    // only reading in a direction is not evidence that it is wrong.
    QVector<QVector3D> lonely = sphere(300, rng);
    const QVector3D isolated(0.0f, 0.0f, -1.0f);
    for (int i = 0; i < lonely.size(); i++) {
        // Clear the neighbourhood so the added sample stands alone and its
        // leverage is near one.
        if (QVector3D::dotProduct(lonely.at(i).normalized(), isolated) > 0.5f) {
            lonely.removeAt(i--);
        }
    }
    lonely.append(isolated * kField);
    const int lonelyIndex = lonely.size() - 1;
    const QVector<float> h = MagRetention().leverages(lonely);
    const MagRetention::Choice onLonely = MagRetention().choose(lonely);
    std::printf("      isolated sample: leverage %.4f, evicted %d as %s\n",
                h.isEmpty() ? -1.0f : h.at(lonelyIndex), onLonely.index,
                onLonely.reason == MagRetention::Reason::Outlier ? "outlier"
                                                                 : "leverage");
    check(onLonely.index != lonelyIndex,
          "a lone high-leverage sample at an honest radius is not an outlier");

    // And the threshold means sigmas: a residual just under it survives, one
    // just over it does not.
    for (float sigmas : { 3.0f, 6.0f }) {
        QVector<QVector3D> probe = sphere(300, rng);
        QVector<float> radii;
        for (const QVector3D &v : probe) {
            radii.append(v.length());
        }
        float mean = 0.0f;
        for (float r : radii) {
            mean += r;
        }
        mean /= radii.size();
        float sq = 0.0f;
        for (float r : radii) {
            sq += (r - mean) * (r - mean);
        }
        const float scale = std::sqrt(sq / radii.size());
        const QVector3D where = probe.at(40).normalized();
        probe.append(where * (mean + sigmas * scale));
        const MagRetention::Choice c = MagRetention().choose(probe);
        const bool flagged = (c.index == probe.size() - 1)
                             && (c.reason == MagRetention::Reason::Outlier);
        std::printf("      %.0f sigma residual: %s\n", sigmas,
                    flagged ? "evicted as an outlier" : "kept");
        check(flagged == (sigmas > 4.0f),
              sigmas > 4.0f ? "a 6 sigma residual is evicted"
                            : "a 3 sigma residual is not");
    }
}

void checkGuards()
{
    std::printf("guards\n");

    std::mt19937 rng(13);
    QVector<QVector3D> samples = sphere(300, rng);

    // Probation: protect the sample leverage would otherwise take.
    MagRetention retention;
    const MagRetention::Choice unguarded = retention.choose(samples);
    QVector<bool> probation(samples.size(), false);
    probation[unguarded.index] = true;
    const MagRetention::Choice guarded = retention.choose(samples, probation);
    check(guarded.index != unguarded.index,
          "a sample on probation is not evicted");
    check(guarded.reason != MagRetention::Reason::None,
          "and another is chosen instead");

    // Patch floor: a sample alone in its patch is never the one evicted.
    const MagRetention::Config cfg = retention.config();
    QVector<int> occupancy(cfg.patchCount, 0);
    for (int i = 0; i < samples.size(); i++) {
        const int p = MagQuality::patch(samples.at(i), cfg.patchCount);
        if (p >= 0) {
            occupancy[p]++;
        }
    }
    int lonely = 0;
    for (int i = 0; i < 200; i++) {
        const MagRetention::Choice c = retention.choose(samples);
        if (c.index < 0) {
            break;
        }
        const int p = MagQuality::patch(samples.at(c.index), cfg.patchCount);
        if (p >= 0 && occupancy.at(p) <= 1) {
            lonely++;
        }
        // Remove it and recount, as the live buffer would.
        occupancy[p]--;
        samples.removeAt(c.index);
    }
    std::printf("      evictions that emptied a patch: %d of 200\n", lonely);
    check(lonely == 0, "coverage is never traded away by an eviction");

    // Everything protected: refuse rather than pick arbitrarily.
    std::mt19937 rng2(17);
    const QVector<QVector3D> fresh = sphere(300, rng2);
    const QVector<bool> allProtected(fresh.size(), true);
    check(MagRetention().choose(fresh, allProtected).reason
              == MagRetention::Reason::None,
          "with every sample protected, no choice is made");
}

} // namespace

int main()
{
    std::printf("MagRetention offline checks\n\n");
    checkLeverageIdentity();
    std::printf("\n");
    checkRankDeficiency();
    std::printf("\n");
    checkRedundancy();
    std::printf("\n");
    checkOutlier();
    std::printf("\n");
    checkGuards();
    std::printf("\n");

    if (failures == 0) {
        std::printf("all checks passed\n");
        return 0;
    }
    std::printf("%d check(s) FAILED\n", failures);
    return 1;
}
