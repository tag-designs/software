#include <cmath>
#include <cfloat>
#include "compassdata.h"


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

bool CompassData::eCompass(QVector3D mag, QVector3D accel, 
                  float& yaw, float& pitch, float& roll, float& dip,
				  float& field)
{
    const float alpha = 0.4;
    if (magcal.ValidMagCal) // only compute if magnetometer is calibrated
    {
		// apply magnetometer calibration

		acc_filt = alpha * acc_filt + (1.0-alpha) * accel;
		accel = acc_filt;
		mag_filt = alpha * mag_filt + (1.0-alpha) * mag;
		mag = mag_filt;
 
		apply_calibration(mag);
		QVector3D h(accel[0],accel[1],0);

		//compute field, roll, pitch
		field = mag.length();
		accel.normalize();
        roll =  atan2(accel.x(), accel.z());                        // Roll in radians
        pitch = atan2(accel.y(), QVector2D(accel).length()); // Pitch in radians
 		pitch = pitch * 180.0f / M_PI;
        roll = roll * 180.0f / M_PI;
		QQuaternion aq = QQuaternion::fromEulerAngles(pitch,0.0,roll);
		aq.normalize();

		/*
		// align magnetometer
        float sinRoll = sin(roll);
        float cosRoll = cos(roll);
        float sinPitch = sin(pitch);
        float cosPitch = cos(pitch);

        float Bx_horiz = mag.x() * cosPitch + mag.y() * sinRoll * sinPitch + mag.z() * cosRoll * sinPitch;
        float By_horiz = mag.y() * cosRoll - mag.z() * sinRoll;
        float Bz_horiz = -mag.x() * sinPitch + mag.y() * sinRoll * cosPitch + mag.z() * cosRoll * cosPitch;

		// compute yaw, dip
		*/

		QVector3D ma = aq.rotatedVector(mag);
		ma.normalize();

        yaw = atan2(ma.x(), ma.y()); // Yaw in radians
        //float mag_horizontal = sqrt(Bx_horiz * Bx_horiz + By_horiz * By_horiz);
        //float dip_angle_rad = atan2(Bz_horiz, mag_horizontal);

		float dip_angle_rad = atan2(ma.z(), QVector2D(ma).length());

		// convert to degrees

        yaw = yaw * 180.0f / M_PI;
		if (yaw < 0.0)
			yaw += 360.0;
       
        dip = dip_angle_rad*180.0f/M_PI;

        return true;
    }
    return false;

}

float CompassData::getField()
{
	return magcal.B;
}
/*
void ComplementaryFilter::getMeasurement(double ax, double ay, double az,
                                         double mx, double my, double mz,
                                         double& q0_meas, double& q1_meas,
                                         double& q2_meas, double& q3_meas)
{
    // q_acc is the quaternion obtained from the acceleration vector
    // representing the orientation of the Global frame wrt the Local frame with
    // arbitrary yaw (intermediary frame). q3_acc is defined as 0.
    double q0_acc, q1_acc, q2_acc, q3_acc;

    // Normalize acceleration vector.
    normalizeVector(ax, ay, az);
    if (az >= 0)
    {
        q0_acc = sqrt((az + 1) * 0.5);
        q1_acc = -ay / (2.0 * q0_acc);
        q2_acc = ax / (2.0 * q0_acc);
        q3_acc = 0;
    } else
    {
        double X = sqrt((1 - az) * 0.5);
        q0_acc = -ay / (2.0 * X);
        q1_acc = X;
        q2_acc = 0;
        q3_acc = ax / (2.0 * X);
    }

    // [lx, ly, lz] is the magnetic field reading, rotated into the intermediary
    // frame by the inverse of q_acc.
    // l = R(q_acc)^-1 m
    double lx = (q0_acc * q0_acc + q1_acc * q1_acc - q2_acc * q2_acc) * mx +
                2.0 * (q1_acc * q2_acc) * my - 2.0 * (q0_acc * q2_acc) * mz;
    double ly = 2.0 * (q1_acc * q2_acc) * mx +
                (q0_acc * q0_acc - q1_acc * q1_acc + q2_acc * q2_acc) * my +
                2.0 * (q0_acc * q1_acc) * mz;

    // q_mag is the quaternion that rotates the Global frame (North West Up)
    // into the intermediary frame. q1_mag and q2_mag are defined as 0.
    double gamma = lx * lx + ly * ly;
    double beta = sqrt(gamma + lx * sqrt(gamma));
    double q0_mag = beta / (sqrt(2.0 * gamma));
    double q3_mag = ly / (sqrt(2.0) * beta);

    // The quaternion multiplication between q_acc and q_mag represents the
    // quaternion, orientation of the Global frame wrt the local frame.
    // q = q_acc times q_mag
    quaternionMultiplication(q0_acc, q1_acc, q2_acc, q3_acc, q0_mag, 0, 0,
                             q3_mag, q0_meas, q1_meas, q2_meas, q3_meas);
    // q0_meas = q0_acc*q0_mag;
    // q1_meas = q1_acc*q0_mag + q2_acc*q3_mag;
    // q2_meas = q2_acc*q0_mag - q1_acc*q3_mag;
    // q3_meas = q0_acc*q3_mag;
}
	*/

bool CompassData::eCompass(QVector3D magin, QVector3D accel, QQuaternion &q, 
                  float& heading, float& dip, float& field)
{
	// qacc is the quaternion obtained from the acceleration vector
    // representing the orientation of the Global frame wrt the Local frame with
    // arbitrary yaw (intermediary frame). qacc[3] is defined as 0.

	QQuaternion qacc;
	float alpha = 0.4;
	float q0,q1,q2,q3;

	acc_filt = alpha * acc_filt + (1.0-alpha) * accel;
	accel = acc_filt;
	mag_filt = alpha * mag_filt + (1.0-alpha) * magin;
	//magin = mag_filt;

	apply_calibration(magin);
	field = magin.length();
	magin.normalize();
	accel.normalize(); 

	// AQUA assumes north west up frame 

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

	qacc = QQuaternion(q0,q1,q2,q3);

	// test the quaternion

	QVector3D atest =  qacc.rotatedVector(QVector3D(0,0,1));

	qInfo() << "A In:  " << accel;
	qInfo() << "A test:" << accel << " == " << atest;
	qInfo() << "A Out: " << qacc.rotatedVector(accel);
	

	// note this step!  Apply inverted quarternion for calculating
	// qmag

	QVector3D mag = magin;
	//mag.setX(-mag.x());
	mag = QVector3D(mag.y(),mag.x(),mag.z());
	//mag = qacc.inverted().rotatedVector(mag);
	//mag = QVector3D(1,2,3);
	mag.normalize();


	// computer magnetometer quaternion

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

	// q3_mag, q0_mag backwards in paper!

	QQuaternion qmag(q3_mag,0,0,q0_mag);

	q = qacc*qmag;
	q.normalize();
	qInfo() << "A test 2" << accel << " == " << q.rotatedVector(QVector3D(0,0,1));
	//q = QQuaternion(q.scalar(),q.y(),q.x(),q.z());
	qInfo() << "A final: " << q.rotatedVector(accel);
	QVector3D magtest(sqrt(gamma), 0, mag.z());
	qInfo() << "Mag test: " << qmag.rotatedVector(mag) << " == " << magtest;
	qInfo() << "M In:  " << magin;
	qInfo() << "M int  " << qmag.rotatedVector(mag);
	qInfo() << "M Out: " << q.rotatedVector(magin);

	//q = QQuaternion(q.scalar(),-q.y(),q.x(),q.z());
	
	//q = qacc;

	magin = q.rotatedVector(magin);
	dip = -atan2(magin[2],magin[1])*180.0/M_PI;

	return true;
}

// the following are from magical


void CompassData::apply_calibration(float rawx, float rawy, float rawz, Point_t *out)
{
	float x, y, z;

	x = rawx - magcal.V[0];
	y = rawy - magcal.V[1];
	z = rawz - magcal.V[2];
	out->x = x * magcal.invW[0][0] + y * magcal.invW[0][1] + z * magcal.invW[0][2];
	out->y = x * magcal.invW[1][0] + y * magcal.invW[1][1] + z * magcal.invW[1][2];
	out->z = x * magcal.invW[2][0] + y * magcal.invW[2][1] + z * magcal.invW[2][2];
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


