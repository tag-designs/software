#include "magquality.h"

#include <algorithm>
#include <cmath>

// MSVC does not define M_PI without _USE_MATH_DEFINES, and this library is
// built on Windows too. magcal.h guards it the same way.
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace {

/// Angle between successive points of a spherical Fibonacci lattice.
const double kGoldenAngle = M_PI * (3.0 - std::sqrt(5.0));

bool finite3(const QVector3D &v)
{
    return std::isfinite(v.x()) && std::isfinite(v.y()) && std::isfinite(v.z());
}

/**
 * @brief Eigenvalues of a symmetric 3x3 matrix, ascending.
 *
 * @details The closed form for the symmetric case: shift by the mean
 *          eigenvalue, scale, and read the three roots off a cosine. No
 *          iteration, so it cannot fail to converge, and the matrix here is a
 *          second moment and therefore always symmetric.
 */
void symmetricEigenvalues(const double a[3][3], double out[3])
{
    const double p1 = a[0][1] * a[0][1] + a[0][2] * a[0][2] + a[1][2] * a[1][2];
    const double q = (a[0][0] + a[1][1] + a[2][2]) / 3.0;

    if (p1 <= 0.0) {
        // Already diagonal.
        out[0] = a[0][0];
        out[1] = a[1][1];
        out[2] = a[2][2];
        std::sort(out, out + 3);
        return;
    }

    const double d0 = a[0][0] - q;
    const double d1 = a[1][1] - q;
    const double d2 = a[2][2] - q;
    const double p2 = d0 * d0 + d1 * d1 + d2 * d2 + 2.0 * p1;
    const double p = std::sqrt(p2 / 6.0);
    if (p <= 0.0) {
        out[0] = out[1] = out[2] = q;
        return;
    }

    double b[3][3];
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            b[i][j] = (a[i][j] - (i == j ? q : 0.0)) / p;
        }
    }

    const double det =
        b[0][0] * (b[1][1] * b[2][2] - b[1][2] * b[2][1])
        - b[0][1] * (b[1][0] * b[2][2] - b[1][2] * b[2][0])
        + b[0][2] * (b[1][0] * b[2][1] - b[1][1] * b[2][0]);

    // Rounding can push this just outside the valid range for acos.
    const double r = std::max(-1.0, std::min(1.0, det / 2.0));
    const double phi = std::acos(r) / 3.0;

    const double e0 = q + 2.0 * p * std::cos(phi);
    const double e2 = q + 2.0 * p * std::cos(phi + 2.0 * M_PI / 3.0);
    const double e1 = 3.0 * q - e0 - e2;   // the trace is preserved

    out[0] = e2;
    out[1] = e1;
    out[2] = e0;
    std::sort(out, out + 3);
}

/// Median of a copy, which the caller supplies already sorted.
float medianOfSorted(const QVector<float> &sorted)
{
    const int n = static_cast<int>(sorted.size());
    if (n == 0) {
        return 0.0f;
    }
    return (n % 2) ? sorted.at(n / 2)
                   : 0.5f * (sorted.at(n / 2 - 1) + sorted.at(n / 2));
}

} // namespace


MagQuality::MagQuality()
    : MagQuality(Config())
{
}

MagQuality::MagQuality(const Config &config)
    : config_(config)
{
    if (config_.magPatches < 1) {
        config_.magPatches = 1;
    }
    // A wider mask would need a wider type in gravityMask_.
    if (config_.gravityPatches < 1) {
        config_.gravityPatches = 1;
    } else if (config_.gravityPatches > 32) {
        config_.gravityPatches = 32;
    }
    reset();
}

void MagQuality::reset()
{
    patchCount_.fill(0, config_.magPatches);
    gravityMask_.fill(0u, config_.magPatches);
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            moment_[i][j] = 0.0;
        }
    }
    dip_.clear();
    samples_ = 0;
    accelSamples_ = 0;
}

QVector3D MagQuality::patchCenter(int index, int patchCount)
{
    if (patchCount < 1 || index < 0 || index >= patchCount) {
        return QVector3D();
    }
    const double z = 1.0 - (2.0 * index + 1.0) / patchCount;
    const double r = std::sqrt(std::max(0.0, 1.0 - z * z));
    const double lon = kGoldenAngle * index;
    return QVector3D(static_cast<float>(r * std::cos(lon)),
                     static_cast<float>(r * std::sin(lon)),
                     static_cast<float>(z));
}

int MagQuality::patch(const QVector3D &v, int patchCount)
{
    if (patchCount < 1 || !finite3(v)) {
        return -1;
    }
    const float length = v.length();
    if (length <= 0.0f) {
        return -1;
    }
    const QVector3D unit = v / length;

    // Linear scan. The lattice is ordered by z, so this could binary-search a
    // slab and check a handful of neighbours, but patchCount is around a
    // hundred and this runs once per buffered sample on a 200 ms timer. The
    // scan it replaces in choose_discard_magcal() was 211,000 distance
    // evaluations per sample added.
    int best = -1;
    float bestDot = -2.0f;
    for (int i = 0; i < patchCount; i++) {
        const float dot = QVector3D::dotProduct(unit, patchCenter(i, patchCount));
        if (dot > bestDot) {
            bestDot = dot;
            best = i;
        }
    }
    return best;
}

bool MagQuality::add(const QVector3D &mag, const QVector3D *accel,
                     float dipDegrees, float oneG)
{
    if (!finite3(mag)) {
        return false;
    }
    const float magLength = mag.length();
    if (magLength <= 0.0f) {
        return false;
    }

    const int index = patch(mag, config_.magPatches);
    if (index < 0) {
        return false;
    }
    patchCount_[index]++;
    samples_++;

    const QVector3D unit = mag / magLength;
    const double u[3] = { unit.x(), unit.y(), unit.z() };
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            moment_[i][j] += u[i] * u[j];
        }
    }

    if (accel == nullptr || !finite3(*accel)) {
        return true;
    }
    const float accelLength = accel->length();
    if (accelLength <= 0.0f) {
        return true;
    }
    accelSamples_++;

    // Attitude diversity does not depend on gravity being clean: a direction
    // is a direction, and a sample taken while the tag was moving still says
    // the tag was at a different attitude. Only the dip statistic needs the
    // accelerometer to be measuring gravity alone, so only it is gated.
    const int gravityIndex = patch(*accel, config_.gravityPatches);
    if (gravityIndex >= 0 && gravityIndex < 32) {
        gravityMask_[index] |= (1u << gravityIndex);
    }

    if (oneG > 0.0f && std::isfinite(dipDegrees)) {
        const float ratio = accelLength / oneG;
        if (std::fabs(ratio - 1.0f) <= config_.accelGate) {
            dip_.append(dipDegrees);
        }
    }
    return true;
}

MagQuality::Result MagQuality::result() const
{
    Result r;
    r.magPatches = config_.magPatches;
    r.samples = samples_;
    r.accelSamples = accelSamples_;

    int occupied = 0;
    int minCount = 0;
    int maxCount = 0;
    const int patches = static_cast<int>(patchCount_.size());
    for (int i = 0; i < patches; i++) {
        const int count = patchCount_.at(i);
        if (count > 0) {
            if (occupied == 0 || count < minCount) {
                minCount = count;
            }
            occupied++;
        }
        if (count > maxCount) {
            maxCount = count;
        }
    }
    r.patchesSeen = occupied;
    r.patchMin = minCount;
    r.patchMax = maxCount;
    r.coverage = config_.magPatches > 0
        ? static_cast<float>(occupied) / static_cast<float>(config_.magPatches)
        : 0.0f;

    if (samples_ > 0) {
        double normalised[3][3];
        for (int i = 0; i < 3; i++) {
            for (int j = 0; j < 3; j++) {
                normalised[i][j] = moment_[i][j] / samples_;
            }
        }
        double eig[3];
        symmetricEigenvalues(normalised, eig);
        r.isotropy = (eig[2] > 0.0)
            ? static_cast<float>(std::max(0.0, eig[0]) / eig[2])
            : 0.0f;
    }

    if (occupied > 0) {
        int cells = 0;
        int withGravity = 0;
        const int masks = static_cast<int>(gravityMask_.size());
        for (int i = 0; i < masks; i++) {
            if (patchCount_.at(i) == 0) {
                continue;
            }
            quint32 mask = gravityMask_.at(i);
            if (mask == 0u) {
                continue;
            }
            withGravity++;
            while (mask != 0u) {
                cells += static_cast<int>(mask & 1u);
                mask >>= 1;
            }
        }
        r.attitudeCells = cells;
        r.attitudeDiversity = withGravity > 0
            ? static_cast<float>(cells) / static_cast<float>(withGravity)
            : 0.0f;
    }

    r.dipSamples = static_cast<int>(dip_.size());
    if (!dip_.isEmpty()) {
        QVector<float> sorted = dip_;
        std::sort(sorted.begin(), sorted.end());
        const float median = medianOfSorted(sorted);

        const int n = static_cast<int>(sorted.size());
        QVector<float> deviations;
        deviations.reserve(n);
        for (int i = 0; i < n; i++) {
            deviations.append(std::fabs(sorted.at(i) - median));
        }
        std::sort(deviations.begin(), deviations.end());

        r.dipMeanDeg = median;
        // 1.4826 makes the MAD match a standard deviation on normal data.
        r.dipSpreadDeg = 1.4826f * medianOfSorted(deviations);
        const int p95 = std::max(0, std::min(n - 1,
                                             static_cast<int>(0.95 * n)));
        r.dipP95Deg = deviations.at(p95);
        r.haveAccelMetrics = true;
    }

    return r;
}
