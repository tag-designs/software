#ifndef COMPASS_DATA_H
#define COMPASS_DATA_H

#include <QWidget>
#include <QVector3D>
#include <QScatter3DSeries>
#include "magcal.h"
#include "ema.h"



class CompassData : public QObject
{
    Q_OBJECT


public:

    explicit CompassData(QObject *parent = nullptr);
    bool addData(float& x, float &y, float &z);
    bool getCalibrationConstants(float *B, float *V, float (*A)[3]);
    void setCalibrationConstants(float B, float *V, float(*A)[3]);
    void calibrationQuality(float& gaps,float& variance, float& wobble, float& fiterror);
    void qualityUpdate();
    void clear();
    void getData(QScatterDataArray& data);
    void getRegionData(QScatterDataArray& data, float magnitude);
    bool eCompass(float mx, float my, float mz, 
                  float ax, float ay, float az,
                  float& yaw, float& pitch, float& roll, float& dip,
                  float& field);

signals:

    void calibration_update(void);

private:

    void apply_calibration(float rawx, float rawy, float rawz, Point_t *out); 
    void raw_data_reset();
    int choose_discard_magcal(void);
    void add_magcal_data(const float *data);
    bool raw_data(const float *data);
    Ema r,p,y,d;
};

#endif