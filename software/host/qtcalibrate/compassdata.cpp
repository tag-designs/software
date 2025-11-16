#include <cmath>
#include <cfloat>
#include "compassdata.h"
#include <iostream>


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
            A[i][j] = magcal.A[i][j];
        }
    }
    return (magcal.ValidMagCal);
}

void CompassData::setCalibrationConstants(float B, float *V, float (*A)[3])
{
	 for (int i = 0; i < 3; i++){
        magcal.V[i] = V[i];
        for (int j = 0; j < 3; j++){
            magcal.A[i][j] = A[i][j] ;
        }
		
    }
	magcal.B = B;
	magcal.ValidMagCal = 4;
}

void CompassData::apply_calibration(QVector3D &mag){
	float x, y, z;

	x = mag[0] - magcal.V[0];
	y = mag[1] - magcal.V[1];
	z = mag[2] - magcal.V[2];
	mag[0] = (x * magcal.invW[0][0] + y * magcal.invW[0][1] + z * magcal.invW[0][2]);
	mag[1] = (x * magcal.invW[1][0] + y * magcal.invW[1][1] + z * magcal.invW[1][2]);
	mag[2] = (x * magcal.invW[2][0] + y * magcal.invW[2][1] + z * magcal.invW[2][2]);
}


float CompassData::getField()
{
	return magcal.B;
}


/*
Keeping a Good Attitude: A Quaternion-Based Orientation Filter for IMUs and MARGs
Authors
Roberto G. Valenti, CUNY City College
Ivan Dryanovski, CUNY Graduate Center
Jizhong Xiao, CUNY City College

Algorithm assumes north west up global frame 
*/

#undef DEBUG
#define DEBUG 1


bool CompassData::eCompass(QVector3D magin, QVector3D accel, QQuaternion &q, 
                            float& dip, float& field)
{
	
	QQuaternion qacc;
	float alpha = 0.4; // filter coefficient
	float q0,q1,q2,q3;

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

	// algorithm uses positive g as up, accelerometer measures this as negative g, swapping all three
	// changes handedness of vector

	if ((magin.length() == 0.0) || (accel.length() == 0.0))
		return false;
	
	field = magin.length();
	accel.normalize(); 
	magin.normalize();

	// switch to NWU convention with gravity positive (accel measures opposite sign)

	//accel[2] = -accel[2];
	accel = QVector3D(accel[0],accel[1],accel[2]);
	magin = QVector3D(magin[1],magin[0],magin[2]);
	
	

	// qacc is the quaternion obtained from the acceleration vector
    // representing the orientation of the Global frame wrt the Local frame with
    // arbitrary yaw (intermediary frame).

	
	
    if (accel.z() >= 0)
    {
        q0 =  sqrt((accel.z() + 1) * 0.5);;
        q1 = -accel.y() / (2.0 * q0);
        q2 = accel.x() / (2.0 * q0);
        q3 = 0;
    } else
    {
        double X = sqrt((1 - accel.z()) * 0.5);
        q0 = -accel.y() / (2.0 * X);
        q1 = X;
        q2 = 0;
        q3 = accel.x() / (2.0 * X);
    }

	// need to correct quaternion for  directional difference in gravity
	//qacc = QQuaternion(0,1,0,0)*QQuaternion(q0,q1,q2,q3)*QQuaternion(0,0,1,0);
	qacc = QQuaternion(q3,-q2,-q1,q0);
	qacc.normalize();
    QVector3D angles;
#ifdef DEBUG_
	// Tests
	angles = qacc.toEulerAngles();
	//fprintf(stderr, "\rPitch: %3.2f Roll: %3.2f", angles[1], angles[0]);
	std::cerr <<  "\rPitch: " << angles[1] << " Roll: " << angles[0];
	//return false;

	
    QVector3D atest1 =  qacc.conjugated().rotatedVector(accel);
	QVector3D atest2 =  qacc.rotatedVector(QVector3D(0,0,1));
	qInfo() << "A In:  " << accel;
	qInfo() << "A test1" << atest1 << " == " << QVector3D(0,0,1);
	qInfo() << "A test2" << accel << " == " << atest2;
	qInfo() << "A Out: " << qacc.rotatedVector(accel);
	
#endif
//
	// q_mag is the quaternion that rotates the Global frame (North West Up)
    // into the intermediary frame. q1_mag and q2_mag are defined as 0.

	// note this step!  Apply conjugated quarternion for calculating
	// qmag

   
	//QVector3D mag = qacc.conjugated().rotatedVector(magin);
	QVector3D mag = qacc.rotatedVector(magin);


	float gamma = mag[0]*mag[0] + mag[1]*mag[1];
	float beta  = sqrt(gamma + mag[0] * sqrt(gamma));
	float betaneg = sqrt(gamma - mag[0]* sqrt(gamma));
	float q0_mag, q3_mag;

	if (mag[0] >= 0.0) {
		q0_mag =  beta/sqrt(2.0*gamma);
		q3_mag = mag[1]/(sqrt(2.0) * beta);
	} else {
		q0_mag = mag[1]/(sqrt(2.0) * betaneg);
		q3_mag = betaneg/sqrt(2.0*gamma);
	}

	QQuaternion qmag(q0_mag,0,0,q3_mag);
	qmag.normalize();

	q = qacc*qmag;
	q.normalize();

	dip = atan2(mag[2],sqrt(gamma))*180.0/M_PI;
#ifdef DEBUG
	angles = q.toEulerAngles();
	//if (angles[2] < 0.0) angles[2] += 360.0;
	//fprintf(stderr, "\rPitch: %3.2f Roll: %3.2f Yaw: %3.2f", angles[1], angles[0],angles[2]);
	//return false;
	//qInfo() << "Pitch:" << angles[1] << " Roll: " << angles[0] << " Yaw:" << angles[2];
	QVector3D magtest(sqrt(gamma), 0, mag[2]);
	qInfo() << "Mag test: " << qmag.rotatedVector(mag) << " == " << magtest;
#endif
	// Compute final quaternion

	//q = QQuaternion(q.scalar(),q.y(),q.x(),q.z());
	//* QQuaternion(0,1,0,0)*QQuaternion(0,0,0,1);

#ifdef DEBUG
	QVector3D mtmp = qacc.rotatedVector(magin);
	float yaw = atan2(mtmp[1],mtmp[0])*180.0/M_PI;
	float yaw2 = asin(q3_mag)*360.0/M_PI;
	// run some tests
	//qInfo() << "A test 3" << accel << " == " << q.rotatedVector(QVector3D(0,0,1));
	//qInfo() << "A final: " << q.rotatedVector(accel);
	//qInfo() << "M In:  " << magin;
	//qInfo() << "M acc:" << qacc.rotatedVector(magin);
	//qInfo() << "M Out: " << q.rotatedVector(magin);
#endif
#ifdef DEBUG
	qInfo() << "yaw " << yaw << " yaw2 " << yaw2;
	qInfo() << "qac: " << qacc.vector();
	qInfo() << "qmag " << qmag.vector();
	//qInfo() << "q: " << q.vector();
	
#endif

	// convert quaternion to ENU

	/*
	𝑖𝑗=𝑘,
	𝑗𝑘=𝑖,
	𝑘𝑖=𝑗,
	𝑗𝑖=−𝑘,
	𝑘𝑗=−𝑖,
	𝑖𝑘=−𝑗
	


	//  simplified
    To swap the X and Y axes in a quaternion, swap the x and y components 
	and negate the w component.  This conversion changes the 
	handedness of the coordinate system, and negating 
	w corrects the rotation for this change.
	*/
	//q = QQuaternion(q.scalar(),q.y(),q.x(),-q.z());
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
				minindex = (random() & 1) ? i : j;
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
			i = random() % MAGBUFFSIZE;
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


