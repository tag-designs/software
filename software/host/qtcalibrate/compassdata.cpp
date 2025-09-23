#include "compassdata.h"


CompassData::CompassData(QObject *parent) : QObject{parent}
{
    raw_data_reset();
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
        Point_t out;
        apply_calibration(x,y,z,&out);
        x = out.x;
        y = out.y;
        z = out.z;
        return true;
    }
    return false;
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
}