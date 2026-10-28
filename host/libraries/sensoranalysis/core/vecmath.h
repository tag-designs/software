/**
 * @file    vecmath.h
 * @brief   Minimal float vector and quaternion types for the Qt-free
 *          sensoranalysis core, bit-compatible with Qt's QVector3D and
 *          QQuaternion.
 *
 * @details The compass solve was written against QVector3D and QQuaternion,
 *          and every stored and displayed orientation so far was produced by
 *          Qt's float arithmetic. These types exist so the core can drop Qt
 *          without changing a single bit of that output. Each operation
 *          reproduces the Qt 6 source expression for expression, including
 *          the details a general-purpose library does differently:
 *
 *          - vector length is std::hypot(x, y, z), not sqrt of a dot product;
 *          - quaternion length is Qt's scaled hypot over x, y, z, w in that
 *            order (hypotScaled());
 *          - normalize() leaves a vector alone when its length is within
 *            1e-5 of 1 or of 0, and a quaternion alone when its length is
 *            within 1e-5 of 0 (qFuzzyIsNull);
 *          - quaternion multiplication uses Qt's factored product, not the
 *            textbook Hamilton expansion, so it rounds differently;
 *          - rotate() is literally q * (0, v) * conjugate(q).
 *
 *          This header is the seam for a later move to glm or Eigen, which
 *          the Kalman-style attitude filters will want. Code above it uses
 *          only the types and free functions declared here, so a swap changes
 *          this file alone. A swap will change results in the last bits: run
 *          the compass_core_parity_check (sensoranalysis/test) before and
 *          after, and treat the difference it reports as a measured,
 *          deliberate change rather than a refactor.
 *
 * @note    Keep the expressions as written. Reordering an operation, or
 *          writing a product the textbook way, changes rounding -- and with
 *          floating-point contraction enabled, which fused multiply-adds the
 *          compiler forms -- and breaks bit compatibility with Qt.
 */
#ifndef SENSORANALYSIS_CORE_VECMATH_H
#define SENSORANALYSIS_CORE_VECMATH_H

#include <array>
#include <cmath>

namespace sensormath
{

/**
 * @struct  Vec3f
 * @brief   Three-component float vector with QVector3D's storage and rounding.
 */
struct Vec3f
{
    float x = 0.0f; ///< First component.
    float y = 0.0f; ///< Second component.
    float z = 0.0f; ///< Third component.
};

/**
 * @struct  Quatf
 * @brief   Float quaternion, scalar first, with QQuaternion's rounding.
 *
 * @details Constructed and stored as (w, x, y, z), the same order as
 *          QQuaternion(scalar, x, y, z). A default-constructed value is the
 *          identity, as QQuaternion's is.
 */
struct Quatf
{
    float w = 1.0f; ///< Scalar part.
    float x = 0.0f; ///< x part of the vector.
    float y = 0.0f; ///< y part of the vector.
    float z = 0.0f; ///< z part of the vector.
};

/// Row-major 3x3 double matrix, as CompassCalibration stores soft iron.
using Mat3d = std::array<std::array<double, 3>, 3>;

/// qFuzzyIsNull(float): true when |value| <= 1e-5.
inline bool fuzzyIsNull(float value)
{
    return (value >= 0 ? value : -value) <= 0.00001f;
}

/// Component-wise difference, as QVector3D operator-.
inline Vec3f operator-(Vec3f a, Vec3f b)
{
    return Vec3f{a.x - b.x, a.y - b.y, a.z - b.z};
}

/// Component-wise accumulation, as QVector3D operator+=.
inline Vec3f &operator+=(Vec3f &a, Vec3f b)
{
    a.x += b.x;
    a.y += b.y;
    a.z += b.z;
    return a;
}

/// Component-wise division by a float, as QVector3D operator/(QVector3D, float).
inline Vec3f operator/(Vec3f v, float divisor)
{
    return Vec3f{v.x / divisor, v.y / divisor, v.z / divisor};
}

/// Euclidean length, as QVector3D::length(): std::hypot of the three parts.
inline float length(Vec3f v)
{
    return std::hypot(v.x, v.y, v.z);
}

/// Dot product, as QVector3D::dotProduct(): x, y, z products summed left to right.
inline float dot(Vec3f a, Vec3f b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

/// True when all three parts are finite (neither infinite nor NaN).
inline bool isFinite(Vec3f v)
{
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

/**
 * @brief   Normalizes in place with QVector3D::normalize()'s rules.
 *
 * @details Does nothing when the length is within 1e-5 of 1 (already unit)
 *          or of 0 (null vector); otherwise divides each part by the length.
 */
inline void normalize(Vec3f &v)
{
    const float len = length(v);
    if (fuzzyIsNull(len - 1.0f) || fuzzyIsNull(len)) {
        return;
    }
    v.x /= len;
    v.y /= len;
    v.z /= len;
}

/**
 * @brief   Length of four floats with Qt's scaled hypot (QtPrivate::QHypotHelper).
 *
 * @details Accumulates (scale, total) left to right so no square overflows,
 *          then returns scale * sqrt(total). Infinity and NaN propagate as Qt
 *          propagates them. QQuaternion::length() calls this with x, y, z, w.
 */
inline float hypotScaled(float first, float second, float third, float fourth)
{
    float scale = first >= 0 ? first : -first;
    float total = 1.0f;
    for (const float next : {second, third, fourth}) {
        if (std::isinf(scale) || (std::isnan(scale) && !std::isinf(next))) {
            total = 1.0f;
            continue;
        }
        if (std::isnan(next)) {
            scale = next;
            total = 1.0f;
            continue;
        }
        const float val = next >= 0 ? next : -next;
        if (!(scale > 0) || std::isinf(next)) {
            scale = val;
            total = 1.0f;
            continue;
        }
        if (!(val > 0)) {
            continue;
        }
        if (val > scale) {
            const float ratio = scale / next;
            total = total * ratio * ratio + 1.0f;
            scale = val;
            continue;
        }
        const float ratio = next / scale;
        total = total + ratio * ratio;
    }
    return std::isfinite(scale) ? scale > 0 ? scale * float(std::sqrt(total)) : 0.0f : scale;
}

/// Quaternion length, as QQuaternion::length().
inline float length(Quatf q)
{
    return hypotScaled(q.x, q.y, q.z, q.w);
}

/**
 * @brief   Normalizes in place with QQuaternion::normalize()'s rules.
 *
 * @details Does nothing when the length is within 1e-5 of 0; otherwise
 *          divides each part by the length. Unlike the vector rule, a
 *          near-unit quaternion is still divided.
 */
inline void normalize(Quatf &q)
{
    const float len = length(q);
    if (fuzzyIsNull(len)) {
        return;
    }
    q.x /= len;
    q.y /= len;
    q.z /= len;
    q.w /= len;
}

/// Conjugate, as QQuaternion::conjugated().
inline Quatf conjugate(Quatf q)
{
    return Quatf{q.w, -q.x, -q.y, -q.z};
}

/**
 * @brief   Quaternion product, as QQuaternion operator*.
 *
 * @details Qt's factored form: eight multiplications instead of sixteen,
 *          and different rounding from the Hamilton expansion.
 */
inline Quatf operator*(const Quatf &q1, const Quatf &q2)
{
    float yy = (q1.w - q1.y) * (q2.w + q2.z);
    float zz = (q1.w + q1.y) * (q2.w - q2.z);
    float ww = (q1.z + q1.x) * (q2.x + q2.y);
    float xx = ww + yy + zz;
    float qq = 0.5f * (xx + (q1.z - q1.x) * (q2.x - q2.y));

    float w = qq - ww + (q1.z - q1.y) * (q2.y - q2.z);
    float x = qq - xx + (q1.x + q1.w) * (q2.x + q2.w);
    float y = qq - yy + (q1.w - q1.x) * (q2.y + q2.z);
    float z = qq - zz + (q1.z + q1.y) * (q2.w - q2.x);

    return Quatf{w, x, y, z};
}

/// Rotates @p v by @p q, as QQuaternion::rotatedVector(): q * (0, v) * conj(q).
inline Vec3f rotate(const Quatf &q, Vec3f v)
{
    const Quatf r = q * Quatf{0.0f, v.x, v.y, v.z} * conjugate(q);
    return Vec3f{r.x, r.y, r.z};
}

} // namespace sensormath

#endif // SENSORANALYSIS_CORE_VECMATH_H
