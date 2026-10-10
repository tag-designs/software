#ifndef MAGCAL_H
#define MAGCAL_H

/*
 * Interface to the Freescale magnetometer calibration solver.
 *
 * Declares what magcal.c and matrix.c define, and nothing else. Those two are
 * Freescale's, BSD-3-Clause, with their own notice; this header is ours,
 * written from them, and replaces one inherited from PJRC's MotionCal that
 * carried no license grant.
 *
 * Writing it again also dropped what had rotted: a Point_t and an
 * apply_calibration() with no definition anywhere, a raw_data() and a
 * raw_data_reset() that named C functions the project never had -- CompassData
 * has methods of its own by those names, which is not the same thing -- a
 * sphere_region() whose only definition went with quality.c, and an
 * OVERSAMPLE_RATIO used once, in a comment.
 *
 * The struct layout is the part that matters. magcal.c indexes these fields
 * directly, so a wrong order or a missing member would compile and then
 * misbehave quietly; the assertions below pin the size and the offsets of
 * every field either the solver or qtcalibrate touches.
 */

#include <stdbool.h>
#include <stdint.h>

/*
 * magcal.c and matrix.c include only this header, and between them want sqrtf
 * and printf, so those come in here rather than being added to files that are
 * otherwise untouched.
 */
#include <math.h>
#include <stdio.h>

/// Magnetometer readings the solver fits at once.
#define MAGBUFFSIZE 650

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Solver state: the sample buffer, the current fit, and its scratch.
 *
 * @details Field order is load-bearing and matches what magcal.c expects.
 *          `V`, `invW`, `B`, `ValidMagCal`, `FitError`, `FitErrorAge`,
 *          `BpFast` and `valid` are the eight qtcalibrate reads or writes;
 *          the rest belong to the solver, and the `tr*` and `mat*`/`vec*`
 *          members are a trial fit and its working space rather than results.
 */
typedef struct {
    float   V[3];               ///< Hard iron offset, uT.
    float   invW[3][3];         ///< Inverse soft iron matrix.
    float   B;                  ///< Geomagnetic field magnitude, uT.
    float   FourBsq;            ///< 4*B*B, uT^2.
    float   FitError;           ///< Fit error, percent.
    float   FitErrorAge;        ///< Fit error, percent, grown with age.
    float   trV[3];             ///< Trial hard iron offset, uT.
    float   trinvW[3][3];       ///< Trial inverse soft iron matrix.
    float   trB;                ///< Trial field magnitude, uT.
    float   trFitErrorpc;       ///< Trial fit error, percent.
    float   A[3][3];            ///< Ellipsoid matrix.
    float   invA[3][3];         ///< Its inverse.
    float   matA[10][10];       ///< Solver scratch.
    float   matB[10][10];       ///< Solver scratch.
    float   vecA[10];           ///< Solver scratch.
    float   vecB[4];            ///< Solver scratch.
    int8_t  ValidMagCal;        ///< 0, or the element count of the solver used.
    float   BpFast[3][MAGBUFFSIZE];  ///< Uncalibrated readings, by axis.
    int8_t  valid[MAGBUFFSIZE]; ///< 1 where that slot holds a reading.
    int16_t MagBufferCount;     ///< Readings held.
} MagCalibration_t;

/// The one instance, defined in magcal.c and shared with qtcalibrate.
extern MagCalibration_t magcal;

/**
 * @brief Attempt a calibration from the buffered readings.
 *
 * @return true when a new fit was accepted, which updates V, invW, B,
 *         ValidMagCal and the fit error.
 */
bool MagCal_Run(MagCalibration_t *magcal);

/*
 * Matrix helpers from matrix.c. The solver calls all but two of these;
 * fmatrixAeqI and fmatrixAeqRenormRotA are defined there and called by
 * nothing, and are declared only so that matrix.c compiles without a missing
 * prototype.
 */
void  f3x3matrixAeqI(float A[][3]);
void  f3x3matrixAeqScalar(float A[][3], float Scalar);
void  f3x3matrixAeqAxScalar(float A[][3], float Scalar);
void  f3x3matrixAeqMinusA(float A[][3]);
void  f3x3matrixAeqInvSymB(float A[][3], float B[][3]);
float f3x3matrixDetA(float A[][3]);
void  eigencompute(float A[][10], float eigval[], float eigvec[][10], int8_t n);
void  fmatrixAeqInvA(float *A[], int8_t iColInd[], int8_t iRowInd[],
                     int8_t iPivot[], int8_t isize);
void  fmatrixAeqI(float *A[], int16_t rc);
void  fmatrixAeqRenormRotA(float A[][3]);

#ifdef __cplusplus
} // extern "C"

#include <cstddef>

/*
 * The layout the header this replaced produced, on every target the project
 * builds for. A dropped, reordered or retyped field changes one of these and
 * fails here rather than in the solver's arithmetic.
 */
static_assert(sizeof(MagCalibration_t) == 9504,
              "MagCalibration_t layout changed");
static_assert(offsetof(MagCalibration_t, V) == 0, "V moved");
static_assert(offsetof(MagCalibration_t, invW) == 12, "invW moved");
static_assert(offsetof(MagCalibration_t, B) == 48, "B moved");
static_assert(offsetof(MagCalibration_t, FitError) == 56, "FitError moved");
static_assert(offsetof(MagCalibration_t, FitErrorAge) == 60,
              "FitErrorAge moved");
static_assert(offsetof(MagCalibration_t, matA) == 192, "matA moved");
static_assert(offsetof(MagCalibration_t, ValidMagCal) == 1048,
              "ValidMagCal moved");
static_assert(offsetof(MagCalibration_t, BpFast) == 1052, "BpFast moved");
static_assert(offsetof(MagCalibration_t, valid) == 8852, "valid moved");
static_assert(offsetof(MagCalibration_t, MagBufferCount) == 9502,
              "MagBufferCount moved");
#endif

#endif // MAGCAL_H
