#ifndef MYSCATTER3D_H
#define MYSCATTER3D_H

#include <QWidget>
#include <QTimer>
//#include <QtDataVisualization/Q3DScatter>
#include <QVector3D>
//#include <QtGraphs/Q3DGraphsItem>
//#include <QtDataVisualization/QtDataVisualization>
#include <Q3DScatterWidgetItem>
#include <QtGraphs/qscatter3dseries.h>
#include <QtGraphs/qscatterdataproxy.h>
#include "sinbin.h"

class MyScatter3D : public QWidget
{
    Q_OBJECT

public:
    explicit MyScatter3D(QWidget *parent = nullptr);
    ~MyScatter3D();
    void addData(QVector3D point);
    void clearData();
    void drawSphere(float radius);
    void setData(QScatterDataArray& data);
    void setRegionData(QScatterDataArray &data);
  
private slots:

    void cameraXRotationChanged(float rotation);

private:

    void rotateMesh(const QQuaternion& rotation);
    Q3DScatterWidgetItem *graph = nullptr;
    QScatter3DSeries *series = nullptr;
    QScatter3DSeries *originSeries = nullptr;
    QScatter3DSeries *sphereSeries = nullptr;
    QScatter3DSeries *regionSeries = nullptr;
    QValue3DAxis *axisX = nullptr;
    QValue3DAxis *axisY = nullptr;
    QValue3DAxis *axisZ = nullptr;
    float maxRange = 50.0;
    float sphereRange = 50.0;

    void adjustRange(float radius);

    SinBin bins = SinBin(10);
};

#endif // MYSCATTER3D_H
