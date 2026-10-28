/**
 * @file    directions_core.h
 * @brief   Direction-space geometry shared by the magnetometer and
 *          accelerometer calibrations: sphere patches and evenness.
 *
 * @details Both sensors are calibrated by turning the tag until its readings
 *          trace a sphere, and both need the same two questions answered
 *          about the directions collected so far: which patch of the sphere
 *          does this one fall in, and is the set of them spread or flat.
 *
 *          They are otherwise independent -- different sensors, different
 *          sample populations, different gates, different retention -- so
 *          this holds the geometry and nothing about either task.
 *
 *          Qt-free, on sensormath::Vec3f; the Qt `Directions` namespace
 *          (directions.h) forwards here. Results are bit-identical to the Qt
 *          code this was moved from (test/calibration_dump.cc).
 *
 * @see     magquality_core.h, accelcalibration_core.h
 */
#ifndef SENSORANALYSIS_CORE_DIRECTIONS_CORE_H
#define SENSORANALYSIS_CORE_DIRECTIONS_CORE_H

#include "vecmath.h"

namespace calibcore
{
namespace directions
{

/**
 * @brief   Centre of one patch of a spherical Fibonacci lattice.
 *
 * @details Equal-area by construction and with no pole clustering, which a
 *          latitude/longitude grid would have.
 *
 * @param[in] index       Patch index, 0 to patchCount - 1.
 * @param[in] patchCount  Lattice size, at least 1.
 *
 * @return  The centre as a unit vector, or a zero vector for an index outside
 *          the lattice or a patchCount below 1.
 */
sensormath::Vec3f patchCenter(int index, int patchCount);

/**
 * @brief   Which patch a direction falls in: the one whose centre is nearest.
 *
 * @param[in] v           Any non-zero vector; only its direction is used.
 * @param[in] patchCount  Lattice size.
 *
 * @return  The patch index, or -1 for a zero, infinite or NaN vector or a
 *          patchCount below 1.
 */
int patch(sensormath::Vec3f v, int patchCount);

/**
 * @brief   Eigenvalues of a symmetric 3x3 matrix, ascending.
 *
 * @details The closed form for the symmetric case: shift by the mean
 *          eigenvalue, scale, and read the three roots off a cosine. No
 *          iteration, so it cannot fail to converge, and the matrices here
 *          are second moments and therefore always symmetric.
 *
 * @param[in]  a    The matrix.
 * @param[out] out  The three eigenvalues, smallest first.
 */
void symmetricEigenvalues(const double a[3][3], double out[3]);

/**
 * @brief   How evenly a set of directions covers the sphere, from 0 to 1.
 *
 * @details The ratio of the smallest to the largest eigenvalue of the second
 *          moment. One is perfectly even; near zero means the directions lie
 *          close to a plane -- the tag was spun about one axis and never
 *          tipped -- which a patch count alone reports as healthy, because
 *          the occupied patches really are spread around that plane.
 *
 * @param[in] moment  Second moment of the unit vectors, summed or averaged:
 *                    the ratio is scale free.
 *
 * @return  The ratio; 0 when the largest eigenvalue is not positive.
 */
float isotropy(const double moment[3][3]);

/**
 * @brief   Adds one direction's outer product to a second moment.
 *
 * @param[in,out] moment  The running sum.
 * @param[in]     v       Any vector; normalized here. A zero or non-finite
 *                        vector is ignored.
 */
void accumulate(double moment[3][3], sensormath::Vec3f v);

} // namespace directions
} // namespace calibcore

#endif // SENSORANALYSIS_CORE_DIRECTIONS_CORE_H
