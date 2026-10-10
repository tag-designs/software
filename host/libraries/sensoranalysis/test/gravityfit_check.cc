/**
 * @file gravityfit_check.cc
 * @brief Offline assertion checks for GravityFit.
 *
 * Synthetic data with a planted offset, so the right answer is known before
 * the fit runs. Build with -DBUILD_SENSORANALYSIS_CHECKS=ON.
 */

#include "gravityfit.h"

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
    std::printf("  %-60s %s\n", what, condition ? "ok" : "FAILED");
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

/// Gravity as a biased sensor would report it, with a little noise.
std::vector<QVector3D> sweep(int n, const QVector3D &offset, std::mt19937 &rng,
                             float noise = 2.0f)
{
    std::normal_distribution<float> jitter(0.0f, noise);
    std::vector<QVector3D> out;
    out.reserve(n);
    for (int i = 0; i < n; i++) {
        QVector3D g = direction(rng) * kOneG + offset;
        g += QVector3D(jitter(rng), jitter(rng), jitter(rng));
        out.push_back(g);
    }
    return out;
}

GravityFit::Result fitAll(const std::vector<QVector3D> &samples, bool twoPass)
{
    GravityFit fit;
    for (const QVector3D &s : samples) {
        fit.add(s, kOneG);
    }
    GravityFit::Result r = fit.result(kOneG);
    if (!twoPass || !r.valid) {
        return r;
    }
    fit.refit(r.offset);
    for (const QVector3D &s : samples) {
        fit.add(s, kOneG);
    }
    const GravityFit::Result second = fit.result(kOneG);
    return second.valid ? second : r;
}

void checkRecovery()
{
    std::printf("offset recovery\n");

    std::mt19937 rng(3);
    // The offset measured on a real tag: 77 mg, mostly on z.
    const QVector3D planted(-28.0f, 0.0f, -72.0f);
    const GravityFit::Result r = fitAll(sweep(800, planted, rng), true);

    check(r.valid, "a full sweep yields a fit");
    const float error = (r.offset - planted).length();
    std::printf("      planted %.1f mg, recovered %.1f mg, error %.2f mg\n",
                planted.length(), r.offset.length(), error);
    check(error < 1.0f, "the planted offset is recovered to within a milli-g");
    std::printf("      fitted radius %.1f mg\n", r.radius);
    check(std::fabs(r.radius - kOneG) < 5.0f, "the fitted radius is one g");

    // Zero offset must come back as zero, not as a small invented one.
    std::mt19937 rng2(5);
    const GravityFit::Result none = fitAll(sweep(800, QVector3D(), rng2), true);
    std::printf("      unbiased sensor: recovered %.2f mg\n",
                none.offset.length());
    check(none.valid && none.offset.length() < 1.0f,
          "an unbiased accelerometer yields no offset");
}

void checkMotionRejection()
{
    std::printf("motion rejection\n");

    std::mt19937 rng(7);
    const QVector3D planted(-28.0f, 0.0f, -72.0f);
    std::vector<QVector3D> samples = sweep(600, planted, rng);

    // A quarter of the sweep taken while the tag was being thrown about. These
    // are not on the gravity sphere and must not drag the centre.
    std::uniform_real_distribution<float> wild(-400.0f, 400.0f);
    for (int i = 0; i < 200; i++) {
        samples.push_back(direction(rng) * kOneG + planted
                          + QVector3D(wild(rng), wild(rng), wild(rng)));
    }

    const GravityFit::Result one = fitAll(samples, false);
    const GravityFit::Result two = fitAll(samples, true);
    std::printf("      one pass %.1f mg error, two passes %.1f mg error\n",
                (one.offset - planted).length(),
                (two.offset - planted).length());
    check(two.valid, "a contaminated sweep still yields a fit");
    check((two.offset - planted).length() <= (one.offset - planted).length(),
          "the second pass is no worse than the first");
    check((two.offset - planted).length() < 5.0f,
          "and lands within five milli-g despite the contamination");
}

void checkRefusal()
{
    std::printf("refusal\n");

    std::mt19937 rng(11);

    // Too few samples to be worth solving.
    GravityFit sparse;
    for (int i = 0; i < 10; i++) {
        sparse.add(direction(rng) * kOneG, kOneG);
    }
    check(!sparse.ready() && !sparse.result(kOneG).valid,
          "too few samples yields no fit");

    // A tag held in one attitude throughout: the samples cluster, and no
    // sphere is determined by them. Inventing a centre here would write a
    // fabricated offset from a sweep that never happened.
    GravityFit stuck;
    std::normal_distribution<float> jitter(0.0f, 2.0f);
    for (int i = 0; i < 400; i++) {
        stuck.add(QVector3D(jitter(rng), jitter(rng), kOneG + jitter(rng)),
                  kOneG);
    }
    const GravityFit::Result r = stuck.result(kOneG);
    std::printf("      one attitude throughout: valid = %s\n",
                r.valid ? "yes" : "no");
    check(!r.valid, "samples from a single attitude yield no fit");

    // A sphere nowhere near one g is a wrong sensitivity constant, not an
    // offset, and must not be reported as one.
    GravityFit wrongScale;
    std::mt19937 rng3(13);
    for (int i = 0; i < 500; i++) {
        wrongScale.add(direction(rng3) * (kOneG * 0.45f), kOneG);
    }
    check(!wrongScale.result(kOneG).valid,
          "a sphere far from one g is refused rather than called an offset");
}

} // namespace

int main()
{
    std::printf("GravityFit offline checks\n\n");
    checkRecovery();
    std::printf("\n");
    checkMotionRejection();
    std::printf("\n");
    checkRefusal();
    std::printf("\n");

    if (failures == 0) {
        std::printf("all checks passed\n");
        return 0;
    }
    std::printf("%d check(s) FAILED\n", failures);
    return 1;
}
