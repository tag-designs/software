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