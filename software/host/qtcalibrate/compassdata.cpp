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

bool CompassData::calibration_constants(float *B, float *V, float (*A)[3])
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

void CompassData::clear()
{
    raw_data_reset();
    quality_reset();
}

void CompassData::calibration_quality(float& gaps,float& variance, float& wobble, float& fiterror)
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
                  float& yaw, float& pitch, float& roll, float &dip)
{

    if (magcal.ValidMagCal)
    {
        Point_t out = {mx,my,mz};
        apply_calibration(mx,my,mz,&out);
        mx = out.x;
        my = out.y;
        mz = out.z;

        roll = atan2(ay, az);                        // Roll in radians
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
