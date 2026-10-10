#include "gravityfit.h"

#include <algorithm>
#include <cmath>

namespace {

/// Solve a small dense system by Gauss-Jordan with partial pivoting.
bool solve4(double a[4][4], const double b[4], double out[4])
{
    double m[4][5];
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            m[i][j] = a[i][j];
        }
        m[i][4] = b[i];
    }

    for (int i = 0; i < 4; i++) {
        int pivot = i;
        for (int r = i + 1; r < 4; r++) {
            if (std::fabs(m[r][i]) > std::fabs(m[pivot][i])) {
                pivot = r;
            }
        }
        if (std::fabs(m[pivot][i]) < 1e-12) {
            // Singular: the samples do not span enough orientations to place a
            // sphere. Refusing is the honest answer; a regularised one would
            // invent an offset from a sweep that never happened.
            return false;
        }
        if (pivot != i) {
            for (int c = 0; c < 5; c++) {
                std::swap(m[i][c], m[pivot][c]);
            }
        }
        for (int r = 0; r < 4; r++) {
            if (r == i) {
                continue;
            }
            const double f = m[r][i] / m[i][i];
            for (int c = i; c < 5; c++) {
                m[r][c] -= f * m[i][c];
            }
        }
    }
    for (int i = 0; i < 4; i++) {
        out[i] = m[i][4] / m[i][i];
    }
    return true;
}

} // namespace


GravityFit::GravityFit()
    : GravityFit(Config())
{
}

GravityFit::GravityFit(const Config &config)
    : config_(config)
{
    reset();
}

void GravityFit::reset()
{
    for (int i = 0; i < 4; i++) {
        atb_[i] = 0.0;
        for (int j = 0; j < 4; j++) {
            ata_[i][j] = 0.0;
        }
    }
    residualSum_ = 0.0;
    count_ = 0;
    pass2Offset_ = QVector3D();
    refining_ = false;
}

void GravityFit::refit(const QVector3D &offset)
{
    const Config kept = config_;
    reset();
    config_ = kept;
    pass2Offset_ = offset;
    refining_ = true;
}

bool GravityFit::add(const QVector3D &accel, float oneG)
{
    if (oneG <= 0.0f || !std::isfinite(accel.x()) || !std::isfinite(accel.y())
        || !std::isfinite(accel.z())) {
        return false;
    }

    // On the second pass the magnitude is judged after correction, which is
    // what lets the gate tighten without rejecting honest orientations.
    const QVector3D probe = refining_ ? accel - pass2Offset_ : accel;
    const float ratio = probe.length() / oneG;
    const float gate = refining_ ? config_.refinedGate : config_.gate;
    if (std::fabs(ratio - 1.0f) > gate) {
        return false;
    }

    // Row of the linear system for |p|^2 = 2 c.p + k, with k = r^2 - |c|^2.
    const double p[3] = { accel.x(), accel.y(), accel.z() };
    const double row[4] = { 2.0 * p[0], 2.0 * p[1], 2.0 * p[2], 1.0 };
    const double rhs = p[0] * p[0] + p[1] * p[1] + p[2] * p[2];

    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            ata_[i][j] += row[i] * row[j];
        }
        atb_[i] += row[i] * rhs;
    }
    residualSum_ += rhs;
    count_++;
    return true;
}

GravityFit::Result GravityFit::result(float oneG) const
{
    Result r;
    r.samples = count_;
    if (count_ < config_.minimumSamples) {
        return r;
    }

    double a[4][4];
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            a[i][j] = ata_[i][j];
        }
    }
    double x[4];
    if (!solve4(a, atb_, x)) {
        return r;
    }

    const double centreSq = x[0] * x[0] + x[1] * x[1] + x[2] * x[2];
    const double radiusSq = x[3] + centreSq;
    if (!(radiusSq > 0.0) || !std::isfinite(radiusSq)) {
        return r;
    }

    r.offset = QVector3D(static_cast<float>(x[0]), static_cast<float>(x[1]),
                         static_cast<float>(x[2]));
    r.radius = static_cast<float>(std::sqrt(radiusSq));

    // A sphere so far from one g is not a zero-g offset; it is a wrong
    // sensitivity constant, a sweep that never turned, or a different sensor.
    // Reporting it as an offset would write a bad constant confidently.
    if (oneG > 0.0f) {
        const float scale = r.radius / oneG;
        if (scale < 0.5f || scale > 1.5f) {
            return r;
        }
    }

    r.valid = true;
    return r;
}
