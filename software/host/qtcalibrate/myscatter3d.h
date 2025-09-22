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

class MyScatter3D : public QWidget
{
    Q_OBJECT

public:
    explicit MyScatter3D(QWidget *parent = nullptr);
    ~MyScatter3D();
    void addData(float x, float y, float z);

private:
    Q3DScatterWidgetItem *graph = nullptr;
    QScatter3DSeries *series = nullptr;
};

#endif // MYSCATTER3D_H
