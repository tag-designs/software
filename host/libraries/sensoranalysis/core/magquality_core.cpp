/**
 * @file    magquality_core.cpp
 * @brief   Magnetometer calibration quality metrics, Qt-free.
 *
 * @details Moved from magquality.cpp, expression for expression, onto
 *          sensormath types and std::vector; see magquality_core.h.
 */
#include "magquality_core.h"

#include "directions_core.h"

#include <algorithm>
#include <cmath>

using sensormath::Vec3f;

namespace {

/// Median of a vector the caller has already sorted; 0 when it is empty.
float medianOfSorted(const std::vector<float> &sorted)
{
    const int n = static_cast<int>(sorted.size());
    if (n == 0) {
        return 0.0f;
    }
    return (n % 2) ? sorted[n / 2]
                   : 0.5f * (sorted[n / 2 - 1] + sorted[n / 2]);
}

} // namespace

namespace calibcore {

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
    patchCount_.assign(config_.magPatches, 0);
    gravityMask_.assign(config_.magPatches, 0u);
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            moment_[i][j] = 0.0;
        }
    }
    patchMagnitude_.assign(config_.magPatches, 0.0);
    magnitudes_.clear();
    dip_.clear();
    samples_ = 0;
    accelSamples_ = 0;
}

bool MagQuality::add(Vec3f mag, const Vec3f *accel, float dipDegrees, float oneG)
{
    if (!sensormath::isFinite(mag)) {
        return false;
    }
    const float magLength = sensormath::length(mag);
    if (magLength <= 0.0f) {
        return false;
    }

    const int index = directions::patch(mag, config_.magPatches);
    if (index < 0) {
        return false;
    }
    patchCount_[index]++;
    samples_++;
    magnitudes_.push_back(magLength);

    patchMagnitude_[index] += magLength;

    const Vec3f unit = mag / magLength;
    const double u[3] = { unit.x, unit.y, unit.z };
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            moment_[i][j] += u[i] * u[j];
        }
    }

    if (accel == nullptr || !sensormath::isFinite(*accel)) {
        return true;
    }
    const float accelLength = sensormath::length(*accel);
    if (accelLength <= 0.0f) {
        return true;
    }
    accelSamples_++;

    // Attitude diversity does not depend on gravity being clean: a direction
    // is a direction, and a sample taken while the tag was moving still says
    // the tag was at a different attitude. Only the dip statistic needs the
    // accelerometer to be measuring gravity alone, so only it is gated.
    const int gravityIndex = directions::patch(*accel, config_.gravityPatches);
    if (gravityIndex >= 0 && gravityIndex < 32) {
        gravityMask_[index] |= (1u << gravityIndex);
    }

    if (oneG > 0.0f && std::isfinite(dipDegrees)) {
        const float ratio = accelLength / oneG;
        if (std::fabs(ratio - 1.0f) <= config_.accelGate) {
            dip_.push_back(dipDegrees);
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
        const int count = patchCount_[i];
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
        // The ratio is scale free, so the moment needs no normalising, but
        // dividing keeps the magnitudes near one whatever the sample count.
        double normalised[3][3];
        for (int i = 0; i < 3; i++) {
            for (int j = 0; j < 3; j++) {
                normalised[i][j] = moment_[i][j] / samples_;
            }
        }
        r.isotropy = directions::isotropy(normalised);
    }

    // Residual statistics. The field is the median magnitude rather than the
    // mean, so that a few bad samples set neither it nor the spread measured
    // against it.
    if (!magnitudes_.empty()) {
        std::vector<float> sorted = magnitudes_;
        std::sort(sorted.begin(), sorted.end());
        r.field = medianOfSorted(sorted);

        if (r.field > 0.0f) {
            const int n = static_cast<int>(sorted.size());
            double square = 0.0;
            std::vector<float> deviations;
            deviations.reserve(n);
            for (int i = 0; i < n; i++) {
                const double d = sorted[i] - r.field;
                square += d * d;
                deviations.push_back(std::fabs(static_cast<float>(d)));
            }
            r.fitError = static_cast<float>(
                100.0 * std::sqrt(square / n) / r.field);

            std::sort(deviations.begin(), deviations.end());
            r.residualSpread = static_cast<float>(
                100.0 * 1.4826 * medianOfSorted(deviations) / r.field);
            const int p95 = std::min(n - 1,
                                     static_cast<int>(0.95 * (n - 1) + 0.5));
            r.residualP95 =
                static_cast<float>(100.0 * deviations[p95] / r.field);
        }
    }

    // Hard iron the fit did not remove. An offset d puts a sample in
    // direction u at |B + d.u|, so the magnitude residual is d.u and
    // regressing residual on direction recovers d. One equation per occupied
    // patch, using its mean residual against its own centre, which is what
    // keeps a long dwell from counting more than a glance.
    if (occupied >= 4 && r.field > 0.0f) {
        double a[3][3] = {{0, 0, 0}, {0, 0, 0}, {0, 0, 0}};
        double rhs[3] = {0, 0, 0};
        for (int i = 0; i < config_.magPatches; i++) {
            const int count = patchCount_[i];
            if (count <= 0) {
                continue;
            }
            const double residual = patchMagnitude_[i] / count - r.field;
            const Vec3f centre = directions::patchCenter(i, config_.magPatches);
            const double c[3] = { centre.x, centre.y, centre.z };
            for (int row = 0; row < 3; row++) {
                for (int col = 0; col < 3; col++) {
                    a[row][col] += c[row] * c[col];
                }
                rhs[row] += residual * c[row];
            }
        }

        // Cramer's rule on a 3x3. Singular only when the occupied patches lie
        // in a plane, which the isotropy figure reports separately.
        const double det =
            a[0][0] * (a[1][1] * a[2][2] - a[1][2] * a[2][1])
            - a[0][1] * (a[1][0] * a[2][2] - a[1][2] * a[2][0])
            + a[0][2] * (a[1][0] * a[2][1] - a[1][1] * a[2][0]);
        if (std::fabs(det) > 1e-9) {
            double out[3];
            for (int col = 0; col < 3; col++) {
                double m[3][3];
                for (int row = 0; row < 3; row++) {
                    for (int k = 0; k < 3; k++) {
                        m[row][k] = (k == col) ? rhs[row] : a[row][k];
                    }
                }
                out[col] =
                    (m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1])
                     - m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0])
                     + m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0])) / det;
            }
            r.residualHardIron = static_cast<float>(
                std::sqrt(out[0] * out[0] + out[1] * out[1] + out[2] * out[2]));
        }
    }

    if (occupied > 0) {
        int cells = 0;
        int withGravity = 0;
        const int masks = static_cast<int>(gravityMask_.size());
        for (int i = 0; i < masks; i++) {
            if (patchCount_[i] == 0) {
                continue;
            }
            std::uint32_t mask = gravityMask_[i];
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
        r.attitudePatches = withGravity;
        r.attitudeDiversity = withGravity > 0
            ? static_cast<float>(cells) / static_cast<float>(withGravity)
            : 0.0f;
    }

    r.dipSamples = static_cast<int>(dip_.size());
    if (!dip_.empty()) {
        std::vector<float> sorted = dip_;
        std::sort(sorted.begin(), sorted.end());
        const float median = medianOfSorted(sorted);

        const int n = static_cast<int>(sorted.size());
        std::vector<float> deviations;
        deviations.reserve(n);
        for (int i = 0; i < n; i++) {
            deviations.push_back(std::fabs(sorted[i] - median));
        }
        std::sort(deviations.begin(), deviations.end());

        r.dipMeanDeg = median;
        // 1.4826 makes the MAD match a standard deviation on normal data.
        r.dipSpreadDeg = 1.4826f * medianOfSorted(deviations);
        const int p95 = std::max(0, std::min(n - 1,
                                             static_cast<int>(0.95 * n)));
        r.dipP95Deg = deviations[p95];
        r.haveAccelMetrics = true;
    }

    return r;
}

} // namespace calibcore
