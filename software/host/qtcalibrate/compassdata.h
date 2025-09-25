#ifndef COMPASS_DATA_H
#define COMPASS_DATA_H

#include <QWidget>
#include <QVector3D>
#include <QScatter3DSeries>
#include "magcal.h"



class CompassData : public QObject
{
    Q_OBJECT


public:

    explicit CompassData(QObject *parent = nullptr);
    bool addData(float& x, float &y, float &z);
    bool calibration_constants(float *B, float *V, float (*A)[3]);
    void calibration_quality(float& gaps,float& variance, float& wobble, float& fiterror);
    void qualityUpdate();
    void clear();
    void getData(QScatterDataArray& data);
    void getRegionData(QScatterDataArray& data, float magnitude);

signals:

    void calibration_update(void);

private:

        //MagCalibration_t magcal;

};

#endif