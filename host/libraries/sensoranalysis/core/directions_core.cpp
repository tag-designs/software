/**
 * @file    directions_core.cpp
 * @brief   Sphere patches and direction evenness for the calibration core.
 *
 * @details Moved from directions.cpp, expression for expression, onto
 *          sensormath types; see directions_core.h.
 */
#include "directions_core.h"

#include <algorithm>
#include <cmath>

#ifndef M_PI
/// Pi. MSVC does not define M_PI without _USE_MATH_DEFINES, and this library
/// is built on Windows too; magcal.h guards it the same way.
#define M_PI 3.14159265358979323846
#endif

using sensormath::Vec3f;

namespace {

/// Angle between successive points of a spherical Fibonacci lattice.
const double kGoldenAngle = M_PI * (3.0 - std::sqrt(5.0));

} // namespace

namespace calibcore {
namespace directions {

Vec3f patchCenter(int index, int patchCount)
{
    if (patchCount < 1 || index < 0 || index >= patchCount) {
        return Vec3f{};
    }
    const double z = 1.0 - (2.0 * index + 1.0) / patchCount;
    const double r = std::sqrt(std::max(0.0, 1.0 - z * z));
    const double lon = kGoldenAngle * index;
    return Vec3f{static_cast<float>(r * std::cos(lon)),
                 static_cast<float>(r * std::sin(lon)),
                 static_cast<float>(z)};
}

int patch(Vec3f v, int patchCount)
{
    if (patchCount < 1 || !sensormath::isFinite(v)) {
        return -1;
    }
    const float len = sensormath::length(v);
    if (len <= 0.0f) {
        return -1;
    }
    const Vec3f unit = v / len;

    // Linear scan. The lattice is ordered by z, so this could binary-search a
    // slab and check a handful of neighbours, but patchCount is at most a few
    // hundred and this runs once per sample on a 200 ms timer. The scan it
    // replaced in choose_discard_magcal() was 211,000 distance evaluations
    // per sample added.
    int best = -1;
    float bestDot = -2.0f;
    for (int i = 0; i < patchCount; i++) {
        const float d = sensormath::dot(unit, patchCenter(i, patchCount));
        if (d > bestDot) {
            bestDot = d;
            best = i;
        }
    }
    return best;
}

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

void accumulate(double moment[3][3], Vec3f v)
{
    if (!sensormath::isFinite(v)) {
        return;
    }
    const float len = sensormath::length(v);
    if (len <= 0.0f) {
        return;
    }
    const double u[3] = { v.x / len, v.y / len, v.z / len };
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            moment[i][j] += u[i] * u[j];
        }
    }
}

float isotropy(const double moment[3][3])
{
    double eig[3];
    symmetricEigenvalues(moment, eig);
    if (eig[2] <= 0.0) {
        return 0.0f;
    }
    return static_cast<float>(std::max(0.0, eig[0]) / eig[2]);
}

} // namespace directions
} // namespace calibcore
