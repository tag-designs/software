#include <cmath>
#include <cfloat>
#include "compassdata.h"
#include "compass_processor.h"
#include <iostream>
#include <random>

namespace
{

CompassCalibration calibrationFromMagcal()
{
	CompassCalibration::Matrix softIron;
	for (int i = 0; i < 3; i++) {
		for (int j = 0; j < 3; j++) {
			softIron[i][j] = magcal.invW[i][j];
		}
	}

	return CompassCalibration::fromMagnetometerConstants(
		QVector3D(magcal.V[0], magcal.V[1], magcal.V[2]),
		softIron);
}

} // namespace


CompassData::CompassData(QObject *parent) : QObject{parent}
{
    clear();
}

bool CompassData::addData(QVector3D &mag) 
{
    bool result = raw_data(mag);
    if (result) {
        emit calibration_update();
    }
    if (magcal.ValidMagCal)
    {
        apply_calibration(mag);
        return true;
    }
    return false;
}

void CompassData::getData(QList<QVector3D> &data)
{
    for (int i=0; i < MAGBUFFSIZE; i++) {
        if (magcal.valid[i]) {
			QVector3D point = BpFast(i);
			apply_calibration(point);
			data.append(point);
        }
    }       
}
/*
void CompassData::getRegionData(QScatterDataArray& data, float magnitude){
    for (int i=0; i < SPHERE_REGIONS; i++){
        QScatterDataItem item(sphereideal[i].x*magnitude,
                              sphereideal[i].y*magnitude,
                              sphereideal[i].z*magnitude);
        data << item;
    }
}
*/

bool CompassData::getCalibrationConstants(float *B, float *V, float (*A)[3])
{
    *B = magcal.B;
    for (int i = 0; i < 3; i++){
        V[i] = magcal.V[i];
        for (int j = 0; j < 3; j++){
            A[i][j] = magcal.invW[i][j];
        }
    }
    return (magcal.ValidMagCal);
}

void CompassData::setCalibrationConstants(float B, float *V, float (*A)[3])
{
	 for (int i = 0; i < 3; i++){
        magcal.V[i] = V[i];
        for (int j = 0; j < 3; j++){
            magcal.invW[i][j] = A[i][j] ;
        }
		
    }
	magcal.B = B;
	magcal.ValidMagCal = 4;
}

void CompassData::apply_calibration(QVector3D &mag){
	mag = calibrationFromMagcal().apply(mag);
}


float CompassData::getField()
{
	return magcal.B;
}


bool CompassData::eCompass(QVector3D magin, QVector3D accel, QQuaternion &q, 
                            float& dip, float& field)
{
	float alpha = 0.4; // filter coefficient

	apply_calibration(magin);

	// lowpass filter inputs

	acc_filt = alpha * acc_filt + (1.0-alpha) * accel;
	mag_filt = alpha * mag_filt + (1.0-alpha) * magin;

	// convert accelerometer to NWU (positive Z pointing down, pos x facing north, pos y facing west
	//accel[0] = acc_filt[1];
	//accel[1] = acc_filt[0];
	//accel[2] = -acc_filt[2];

//#ifndef DEBUG
	accel = acc_filt;
	magin = mag_filt;
//#endif

	CompassRawSample raw;
	raw.accel = accel;
	raw.mag = magin;

	// qtcalibrate keeps ownership of live calibration and low-pass filtering.
	// The UI-free eCompass solve itself is shared with compviz and sensorviz.
	CompassProcessor processor{CompassCalibration()};
	CompassDerivedSample derived;
	if (!processor.deriveCalibratedSample(raw, derived))
		return false;

	q = derived.q;
	dip = derived.dip;
	field = derived.field;
	return true;
}

// the following are from magical; they handle data management for
// the NXP calibration routines

void CompassData::clear()
{
    raw_data_reset();
    quality_reset();
}

void CompassData::calibrationQuality(float& gaps,float& variance, float& wobble, float& fiterror)
{ 
    gaps = quality_surface_gap_error();
    variance = quality_magnitude_variance_error();
    wobble = quality_wobble_error();
    fiterror = quality_spherical_fit_error();
}

void CompassData::qualityUpdate(){
    quality_reset();
    for (int i=0; i < MAGBUFFSIZE; i++) {
        if (magcal.valid[i]) {
			QVector3D point = BpFast(i);
			apply_calibration(point);
			Point_t pt = {point.x(),point.y(),point.z()};
            quality_update(&pt);
        }
    }       
}

void CompassData::raw_data_reset(void)
{
	//rawcount = OVERSAMPLE_RATIO;
	//fusion_init();
	memset((void*) &magcal, 0, sizeof(magcal));
	magcal.invW[0][0] = 1.0f;
	magcal.invW[1][1] = 1.0f;
	magcal.invW[2][2] = 1.0f;
	magcal.FitError = 100.0f;
	magcal.FitErrorAge = 100.0f;
	magcal.B = 50.0f;
}

int CompassData::choose_discard_magcal(void)
{
	//int32_t rawx, rawy, rawz;
	//int32_t dx, dy, dz;
	//float x, y, z;
	float dist, mindist=FLT_MAX;
	//uint64_t distsq, minsum=0xFFFFFFFFFFFFFFFFull;
	static int runcount=0;
	int i, j, minindex=0;
	Point_t point;
	float gaps, field, error, errormax;

	// When enough data is collected (gaps error is low), assume we
	// have a pretty good coverage and the field stregth is knownn.
	gaps = quality_surface_gap_error();
	if (gaps < 25.0f) {
		// occasionally look for points farthest from average field strength
		// always rate limit assumption-based data purging, but allow the
		// rate to increase as the angular coverage improves.
		if (gaps < 1.0f) gaps = 1.0f;
		if (++runcount > (int)(gaps * 10.0f)) {
			j = MAGBUFFSIZE;
			errormax = 0.0f;
			for (i=0; i < MAGBUFFSIZE; i++) {
				QVector3D point = BpFast(i);
				apply_calibration(point);
				field = point.length();
				// if magcal.B is bad, things could go horribly wrong
				error = fabsf(field - magcal.B);
				if (error > errormax) {
					errormax = error;
					j = i;
				}
			}
			runcount = 0;
			if (j < MAGBUFFSIZE) {
				//printf("worst error at %d\n", j);
				return j;
			}
		}
	} else {
		runcount = 0;
	}
	// When solid info isn't available, find 2 points closest to each other,
	// and randomly discard one.  When we don't have good coverage, this
	// approach tends to add points into previously unmeasured areas while
	// discarding info from areas with highly redundant info.
	for (i=0; i < MAGBUFFSIZE; i++) {
		for (j=i+1; j < MAGBUFFSIZE; j++) {
			QVector3D pt1 = BpFast(i);
			QVector3D pt2 = BpFast(j);
			dist = pt1.distanceToPoint(pt2);
			if (dist < mindist) {
				mindist = dist;
				minindex = (std::rand() & 1) ? i : j;
			}
		}
	}
	return minindex;
}


void CompassData::add_magcal_data(QVector3D data)
{
	int i;

	// first look for an unused caldata slot
	for (i=0; i < MAGBUFFSIZE; i++) {
		if (!magcal.valid[i]) break;
	}
	// If the buffer is full, we must choose which old data to discard.
	// We must choose wisely!  Throwing away the wrong data could prevent
	// collecting enough data distributed across the entire 3D angular
	// range, preventing a decent cal from ever happening at all.  Making
	// any assumption about good vs bad data is particularly risky,
	// because being wrong could cause an unstable feedback loop where
	// bad data leads to wrong decisions which leads to even worse data.
	// But if done well, purging bad data has massive potential to
	// improve results.  The trick is telling the good from the bad while
	// still in the process of learning what's good...
	if (i >= MAGBUFFSIZE) {
		i = choose_discard_magcal();
		if (i < 0 || i >= MAGBUFFSIZE) {
			i = std::rand() % MAGBUFFSIZE;
		}
	}
	// add it to the cal buffer
	magcal.BpFast[0][i] = data[0];
	magcal.BpFast[1][i] = data[1];
	magcal.BpFast[2][i] = data[2];
	magcal.valid[i] = 1;
}


bool CompassData::raw_data(QVector3D data)
{
	add_magcal_data(data);
	return MagCal_Run(&magcal) != 0;
}



QVector3D CompassData::BpFast(int i){
	if ((i < 0) || (i >= MAGBUFFSIZE)){
		return QVector3D();
	}
	else {
		return QVector3D(magcal.BpFast[0][i], 
                		 magcal.BpFast[1][i],
                		 magcal.BpFast[2][i]);
	}

}
