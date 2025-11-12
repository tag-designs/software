#ifndef COMPASS_DATA_H
#define COMPASS_DATA_H

#include <QWidget>
#include <QList>
#include <QVector3D>
#include <QQuaternion>
#include <QMatrix3x3>

#include "magcal.h"
#include "ema.h"



class CompassData : public QObject
{
    Q_OBJECT


public:

    explicit CompassData(QObject *parent = nullptr);
    bool addData(QVector3D &mag);//float& x, float &y, float &z);
    void getData(QList<QVector3D> &data);
    bool getCalibrationConstants(float *B, float *V, float (*A)[3]);
    void setCalibrationConstants(float B, float *V, float(*A)[3]);
    void calibrationQuality(float& gaps,float& variance, float& wobble, float& fiterror);
    void qualityUpdate();
    void clear();
 
    //void getRegionData(QScatterDataArray& data, float magnitude);

    bool eCompass(QVector3D mag, QVector3D accel, QQuaternion &q, 
                  float& dip, float& field);
    float getField();

signals:

    void calibration_update(void);

private:

    void apply_calibration(QVector3D &mag);
    //void apply_calibration(float rawx, float rawy, float rawz, Point_t *out); 
    void raw_data_reset();
    int choose_discard_magcal(void);
    void add_magcal_data(QVector3D);
    bool raw_data(QVector3D);
    QVector3D BpFast(int i);
    Ema r,p,y,d,f;
    QVector3D acc_filt, mag_filt;
};

#endif