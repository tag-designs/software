#ifndef MAGCAL_H
#define MAGCAL_H

#define MAGBUFFSIZE 650
#define OVERSAMPLE_RATIO 4

#include <stdint.h>
#include <math.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C"{
#endif

#include <stdlib.h>
#include <string.h>
#include <log.h>

typedef struct {
	float x;
	float y;
	float z;
} Point_t;

// magnetic calibration & buffer structure
typedef struct {
    float V[3];                  // current hard iron offset x, y, z, (uT)
    float invW[3][3];            // current inverse soft iron matrix
    float B;                     // current geomagnetic field magnitude (uT)
    float FourBsq;               // current 4*B*B (uT^2)
    float FitError;              // current fit error %
    float FitErrorAge;           // current fit error % (grows automatically with age)
    float trV[3];                // trial value of hard iron offset z, y, z (uT)
    float trinvW[3][3];          // trial inverse soft iron matrix size
    float trB;                   // trial value of geomagnetic field magnitude in uT
    float trFitErrorpc;          // trial value of fit error %
    float A[3][3];               // ellipsoid matrix A
    float invA[3][3];            // inverse of ellipsoid matrix A
    float matA[10][10];          // scratch 10x10 matrix used by calibration algorithms
    float matB[10][10];          // scratch 10x10 matrix used by calibration algorithms
    float vecA[10];              // scratch 10x1 vector used by calibration algorithms
    float vecB[4];               // scratch 4x1 vector used by calibration algorithms
    int8_t ValidMagCal;          // integer value 0, 4, 7, 10 denoting both valid calibration and solver used
    float BpFast[3][MAGBUFFSIZE];   // uncalibrated magnetometer readings
    int8_t  valid[MAGBUFFSIZE];        // 1=has data, 0=empty slot
    int16_t MagBufferCount;           // number of magnetometer readings
} MagCalibration_t;

extern MagCalibration_t magcal;

void f3x3matrixAeqI(float A[][3]);
void fmatrixAeqI(float *A[], int16_t rc);
void f3x3matrixAeqScalar(float A[][3], float Scalar);
void f3x3matrixAeqInvSymB(float A[][3], float B[][3]);
void f3x3matrixAeqAxScalar(float A[][3], float Scalar);
void f3x3matrixAeqMinusA(float A[][3]);
float f3x3matrixDetA(float A[][3]);
void eigencompute(float A[][10], float eigval[], float eigvec[][10], int8_t n);
void fmatrixAeqInvA(float *A[], int8_t iColInd[], int8_t iRowInd[], int8_t iPivot[], int8_t isize);
void fmatrixAeqRenormRotA(float A[][3]);

void raw_data_reset(void);
bool raw_data(const float *data);

void apply_calibration(float rawx, float rawy, float rawz, Point_t *out);
bool MagCal_Run(MagCalibration_t *magcal);

void quality_reset(void);
void quality_update(const Point_t *point);
float quality_surface_gap_error(void);
float quality_magnitude_variance_error(void);
float quality_wobble_error(void);
float quality_spherical_fit_error(void);

#define SPHERE_REGIONS 100
extern Point_t sphereideal[SPHERE_REGIONS];
int sphere_region(float x, float y, float z);


#ifdef __cplusplus
} // extern "C"
#endif
#endif