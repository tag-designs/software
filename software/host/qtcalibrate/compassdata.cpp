#include <cmath>
#include "compassdata.h"


CompassData::CompassData(QObject *parent) : QObject{parent}
{
    clear();
}

bool CompassData::addData(float& x, float& y, float &z) 
{
    float data[] = {x,y,z};
    bool result = raw_data(data);
    if (result) {
        emit calibration_update();
        //qInfo() << result << ", ValidMagCal: " << magcal.ValidMagCal ;
    }
    if (magcal.ValidMagCal)
    {
        Point_t out = {x,y,z};
        apply_calibration(x,y,z,&out);
        x = out.x;
        y = out.y;
        z = out.z;
        return true;
    }
    return false;
}

void CompassData::getData(QScatterDataArray& data)
{
    Point_t point;
    for (int i=0; i < MAGBUFFSIZE; i++) {
        if (magcal.valid[i]) {
            apply_calibration(magcal.BpFast[0][i], 
                magcal.BpFast[1][i],
                magcal.BpFast[2][i], &point);
            //data.push_back(QScatterDataItem
            QScatterDataItem item(point.x,point.y,point.z);
            data << item;
        }
    }       

}

void CompassData::getRegionData(QScatterDataArray& data, float magnitude){
    for (int i=0; i < SPHERE_REGIONS; i++){
        QScatterDataItem item(sphereideal[i].x*magnitude,
                              sphereideal[i].y*magnitude,
                              sphereideal[i].z*magnitude);
        data << item;
    }
}

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
    Point_t point;
    quality_reset();
    for (int i=0; i < MAGBUFFSIZE; i++) {
        if (magcal.valid[i]) {
            apply_calibration(magcal.BpFast[0][i], 
                magcal.BpFast[1][i],
                magcal.BpFast[2][i], &point);
            quality_update(&point);
        }
    }       
}


bool CompassData::eCompass(float mx, float my, float mz, 
                  float ax, float ay, float az,
                  float& yaw, float& pitch, float& roll, float& dip,
				  float& field)
{

    if (magcal.ValidMagCal)
    {
        Point_t out = {mx,my,mz};
        apply_calibration(mx,my,mz,&out);
        mx = out.x;
        my = out.y;
        mz = out.z;

		field = sqrt(mx*mx + my*my + mz*mz);

        roll =  atan2(ay, az);                        // Roll in radians
        pitch = atan2(-ax, sqrt(ay * ay + az * az)); // Pitch in radians

        float sinRoll = sin(roll);
        float cosRoll = cos(roll);
        float sinPitch = sin(pitch);
        float cosPitch = cos(pitch);

        float Bx_horiz = mx * cosPitch + my * sinRoll * sinPitch + mz * cosRoll * sinPitch;
        float By_horiz = my * cosRoll - mz * sinRoll;

        float Bz_horiz = -mx * sinPitch + my * sinRoll * cosPitch + mz * cosRoll * cosPitch;

        yaw = atan2(By_horiz, Bx_horiz); // Yaw in radians

        float mag_horizontal = sqrt(Bx_horiz * Bx_horiz + By_horiz * By_horiz);
        float dip_angle_rad = atan2(Bz_horiz, mag_horizontal);

        yaw = yaw * 180.0f / M_PI;
        pitch = pitch * 180.0f / M_PI;
        roll = roll * 180.0f / M_PI;
        dip = dip_angle_rad*180.0f/M_PI;


        return true;
    }
    return false;

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
	int32_t rawx, rawy, rawz;
	int32_t dx, dy, dz;
	float x, y, z;
	uint64_t distsq, minsum=0xFFFFFFFFFFFFFFFFull;
	static int runcount=0;
	int i, j, minindex=0;
	Point_t point;
	float gaps, field, error, errormax;

	// When enough data is collected (gaps error is low), assume we
	// have a pretty good coverage and the field stregth is known.
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
				rawx = magcal.BpFast[0][i];
				rawy = magcal.BpFast[1][i];
				rawz = magcal.BpFast[2][i];
				apply_calibration(rawx, rawy, rawz, &point);
				x = point.x;
				y = point.y;
				z = point.z;
				field = sqrtf(x * x + y * y + z * z);
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
			dx = magcal.BpFast[0][i] - magcal.BpFast[0][j];
			dy = magcal.BpFast[1][i] - magcal.BpFast[1][j];
			dz = magcal.BpFast[2][i] - magcal.BpFast[2][j];
			distsq = (int64_t)dx * (int64_t)dx;
			distsq += (int64_t)dy * (int64_t)dy;
			distsq += (int64_t)dz * (int64_t)dz;
			if (distsq < minsum) {
				minsum = distsq;
				minindex = (random() & 1) ? i : j;
			}
		}
	}
	return minindex;
}


void CompassData::add_magcal_data(const float *data)
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


bool CompassData::raw_data(const float *data)
{
	
	add_magcal_data(data);
	
	return MagCal_Run(&magcal) != 0;

}


