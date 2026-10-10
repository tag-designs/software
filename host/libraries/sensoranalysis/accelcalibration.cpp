#include "accelcalibration.h"

#include "directions.h"

#include <algorithm>
#include <cmath>

namespace {

/// Gauss-Jordan with partial pivoting on a 4x4. Same shape as the solve in
/// GravityFit; kept local because the matrix here is assembled differently.
bool solve4(double a[4][4], const double b[4], double x[4])
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
        if (!(std::fabs(m[pivot][i]) > 0.0) || !std::isfinite(m[pivot][i])) {
            return false;
        }
        if (pivot != i) {
            for (int c = i; c < 5; c++) {
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
        x[i] = m[i][4] / m[i][i];
        if (!std::isfinite(x[i])) {
            return false;
        }
    }
    return true;
}

} // namespace


AccelCalibration::AccelCalibration()
    : AccelCalibration(Config())
{
}

AccelCalibration::AccelCalibration(const Config &config)
    : config_(config), offered_(0), gated_(0), haveGateOffset_(false)
{
    if (config_.patches < 1) {
        config_.patches = 1;
    }
    if (config_.minimumPatches < 1) {
        config_.minimumPatches = 1;
    }
    if (config_.minimumPatches > config_.patches) {
        config_.minimumPatches = config_.patches;
    }
    reset();
}

void AccelCalibration::reset()
{
    Patch empty;
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            empty.ata[i][j] = 0.0;
        }
        empty.atb[i] = 0.0;
    }
    empty.rhsSquares = 0.0;
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            empty.moment[i][j] = 0.0;
        }
    }
    empty.count = 0;

    patches_.fill(empty, config_.patches);
    offered_ = 0;
    gated_ = 0;
    gateOffset_ = QVector3D();
    haveGateOffset_ = false;
}

bool AccelCalibration::add(const QVector3D &accel, float oneG)
{
    if (!Directions::finite(accel) || !(oneG > 0.0f)) {
        return false;
    }
    offered_++;

    // Motion, not gravity. Judged on the raw magnitude: the offset being
    // estimated is itself several percent of one g, so a gate tight enough to
    // need correcting first would reject orientations rather than movement.
    const QVector3D probe = haveGateOffset_ ? accel - gateOffset_ : accel;
    const float gate = haveGateOffset_ ? config_.refinedGate : config_.gate;
    if (std::fabs(probe.length() - oneG) > gate * oneG) {
        gated_++;
        return false;
    }

    const int index = Directions::patch(accel, config_.patches);
    if (index < 0) {
        gated_++;
        return false;
    }

    // Row of the linear system for |p|^2 = 2 c.p + k, with k = r^2 - |c|^2.
    const double p[3] = { accel.x(), accel.y(), accel.z() };
    const double row[4] = { 2.0 * p[0], 2.0 * p[1], 2.0 * p[2], 1.0 };
    const double rhs = p[0] * p[0] + p[1] * p[1] + p[2] * p[2];

    Patch &patch = patches_[index];
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            patch.ata[i][j] += row[i] * row[j];
        }
        patch.atb[i] += row[i] * rhs;
    }
    patch.rhsSquares += rhs * rhs;
    Directions::accumulate(patch.moment, accel);
    patch.count++;
    return true;
}

QVector<bool> AccelCalibration::occupancy() const
{
    QVector<bool> out;
    out.reserve(config_.patches);
    for (int index = 0; index < config_.patches; index++) {
        out.append(patches_.at(index).count > 0);
    }
    return out;
}

AccelCalibration::Result AccelCalibration::result(float oneG)
{
    Result r;
    r.patches = config_.patches;
    r.offered = offered_;
    r.gated = gated_;

    // Each occupied patch contributes its mean row, so a direction the
    // operator dwelt in counts once, exactly like one they passed through.
    double ata[4][4] = {{0.0, 0.0, 0.0, 0.0}, {0.0, 0.0, 0.0, 0.0},
                        {0.0, 0.0, 0.0, 0.0}, {0.0, 0.0, 0.0, 0.0}};
    double atb[4] = { 0.0, 0.0, 0.0, 0.0 };
    double rhsSquares = 0.0;
    double moment[3][3] = {{0.0, 0.0, 0.0}, {0.0, 0.0, 0.0}, {0.0, 0.0, 0.0}};

    for (int index = 0; index < config_.patches; index++) {
        const Patch &patch = patches_.at(index);
        if (patch.count == 0) {
            continue;
        }
        r.patchesSeen++;
        r.samples += patch.count;

        const double weight = 1.0 / static_cast<double>(patch.count);
        for (int i = 0; i < 4; i++) {
            for (int j = 0; j < 4; j++) {
                ata[i][j] += patch.ata[i][j] * weight;
            }
            atb[i] += patch.atb[i] * weight;
        }
        rhsSquares += patch.rhsSquares * weight;
        for (int i = 0; i < 3; i++) {
            for (int j = 0; j < 3; j++) {
                moment[i][j] += patch.moment[i][j] * weight;
            }
        }
    }

    r.coverage = config_.patches > 0
        ? static_cast<float>(r.patchesSeen) / static_cast<float>(config_.patches)
        : 0.0f;
    if (r.patchesSeen > 0) {
        double normalised[3][3];
        for (int i = 0; i < 3; i++) {
            for (int j = 0; j < 3; j++) {
                normalised[i][j] = moment[i][j] / r.patchesSeen;
            }
        }
        r.isotropy = Directions::isotropy(normalised);
    }

    // Coverage preconditions before the arithmetic, so a fit that comes back
    // valid is one the geometry supports and not merely one the normal
    // equations happened to solve.
    if (r.patchesSeen < config_.minimumPatches
        || r.isotropy < config_.minimumIsotropy) {
        return r;
    }

    double x[4];
    if (!solve4(ata, atb, x)) {
        return r;
    }

    const double centreSq = x[0] * x[0] + x[1] * x[1] + x[2] * x[2];
    const double radiusSq = x[3] + centreSq;
    if (!(radiusSq > 0.0) || !std::isfinite(radiusSq)) {
        return r;
    }

    const QVector3D offset(static_cast<float>(x[0]), static_cast<float>(x[1]),
                           static_cast<float>(x[2]));
    const float radius = static_cast<float>(std::sqrt(radiusSq));

    // A sphere this far from one g is not a zero-g offset; it is a wrong
    // sensitivity constant, or a different sensor. Reporting it as an offset
    // would write a bad constant confidently.
    if (oneG > 0.0f) {
        const float scale = radius / oneG;
        if (scale < 0.5f || scale > 1.5f) {
            return r;
        }
    }

    // RMS distance from the fitted sphere, from the same weighted sums:
    // sum (rhs - row.x)^2 = sum rhs^2 - 2 x.atb + x^T ata x, which is in units
    // of length squared. For a sample near the surface f = |p-c|^2 - r^2 is
    // about 2r times its distance from it. See GravityFit for the same step.
    double square = rhsSquares;
    for (int i = 0; i < 4; i++) {
        square -= 2.0 * x[i] * atb[i];
        for (int j = 0; j < 4; j++) {
            square += x[i] * ata[i][j] * x[j];
        }
    }
    if (square > 0.0 && std::isfinite(square) && radius > 0.0f) {
        const double rms = std::sqrt(square / r.patchesSeen);
        r.residual = static_cast<float>(rms / (2.0 * radius));
    }

    r.valid = true;
    r.offset = offset;
    r.radius = radius;

    // From here the intake gate judges a reading after correcting by this,
    // which is what lets it tighten without rejecting honest orientations.
    gateOffset_ = offset;
    haveGateOffset_ = true;
    return r;
}
