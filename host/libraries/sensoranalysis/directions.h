#ifndef DIRECTIONS_H
#define DIRECTIONS_H

#include <QVector3D>

/*
 * Direction-space geometry shared by the two calibration tasks.
 *
 * Both the magnetometer and the accelerometer are calibrated by turning the
 * tag until its readings trace a sphere, and both need the same two questions
 * answered about the directions collected so far: which patch of the sphere
 * does this one fall in, and is the set of them spread or flat.
 *
 * They are otherwise independent -- different sensors, different sample
 * populations, different gates, different retention -- so this holds the
 * geometry and nothing about either task.
 */
namespace Directions
{

/**
 * @brief Centre of one patch of a spherical Fibonacci lattice.
 *
 * @details Equal-area by construction and with no pole clustering, which a
 *          latitude/longitude grid would have. Returns a null vector for an
 *          index outside the lattice.
 */
QVector3D patchCenter(int index, int patchCount);

/**
 * @brief Which patch a direction falls in.
 *
 * @param v           Any non-zero vector; only its direction is used.
 * @param patchCount  Lattice size.
 *
 * @return The patch index, or -1 for a zero, infinite or NaN vector.
 */
int patch(const QVector3D &v, int patchCount);

/**
 * @brief Eigenvalues of a symmetric 3x3 matrix, ascending.
 *
 * @details The closed form for the symmetric case: shift by the mean
 *          eigenvalue, scale, and read the three roots off a cosine. No
 *          iteration, so it cannot fail to converge, and the matrices here
 *          are second moments and therefore always symmetric.
 */
void symmetricEigenvalues(const double a[3][3], double out[3]);

/**
 * @brief How evenly a set of directions covers the sphere, from 0 to 1.
 *
 * @param moment  Second moment of the unit vectors, summed not averaged.
 *
 * @details The ratio of the smallest to the largest eigenvalue. One is
 *          perfectly even; near zero means the directions lie close to a
 *          plane -- the tag was spun about one axis and never tipped -- which
 *          a patch count alone reports as healthy, because the occupied
 *          patches really are spread around that plane.
 */
float isotropy(const double moment[3][3]);

/// Accumulate one direction into a second moment. Ignores a zero vector.
void accumulate(double moment[3][3], const QVector3D &v);

/// True for a vector with three finite components.
bool finite(const QVector3D &v);

} // namespace Directions

#endif // DIRECTIONS_H
