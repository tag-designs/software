/**
 * @file    directions.h
 * @brief   Direction-space geometry shared by the magnetometer and
 *          accelerometer calibrations, on Qt types.
 *
 * @details An adapter: each function converts its QVector3D and forwards to
 *          calibcore::directions (core/directions_core.h), which holds the
 *          implementation and its rationale. Results are bit-identical to the
 *          Qt implementation this replaced.
 */
#ifndef DIRECTIONS_H
#define DIRECTIONS_H

#include <QVector3D>

/**
 * @namespace Directions
 * @brief   Sphere patches and evenness for QVector3D; see
 *          calibcore::directions.
 */
namespace Directions
{

/**
 * @brief   Centre of one patch of a spherical Fibonacci lattice.
 * @param[in] index       Patch index, 0 to patchCount - 1.
 * @param[in] patchCount  Lattice size.
 * @return  The unit centre, or a null vector for an index outside the lattice.
 * @see     calibcore::directions::patchCenter()
 */
QVector3D patchCenter(int index, int patchCount);

/**
 * @brief   Which patch a direction falls in.
 * @param[in] v           Any non-zero vector; only its direction is used.
 * @param[in] patchCount  Lattice size.
 * @return  The patch index, or -1 for a zero, infinite or NaN vector.
 * @see     calibcore::directions::patch()
 */
int patch(const QVector3D &v, int patchCount);

/**
 * @brief   Eigenvalues of a symmetric 3x3 matrix, ascending.
 * @param[in]  a    The matrix.
 * @param[out] out  The eigenvalues, smallest first.
 * @see     calibcore::directions::symmetricEigenvalues()
 */
void symmetricEigenvalues(const double a[3][3], double out[3]);

/**
 * @brief   How evenly a set of directions covers the sphere, from 0 to 1.
 * @param[in] moment  Second moment of the unit vectors, summed or averaged.
 * @return  Smallest over largest eigenvalue; 0 for an empty moment.
 * @see     calibcore::directions::isotropy()
 */
float isotropy(const double moment[3][3]);

/**
 * @brief   Adds one direction to a second moment; ignores a zero or
 *          non-finite vector.
 * @param[in,out] moment  The running sum.
 * @param[in]     v       Any vector; normalized here.
 */
void accumulate(double moment[3][3], const QVector3D &v);

/**
 * @brief   Whether all three components are finite.
 * @param[in] v  The vector.
 * @return  false when any component is infinite or NaN.
 */
bool finite(const QVector3D &v);

} // namespace Directions

#endif // DIRECTIONS_H
