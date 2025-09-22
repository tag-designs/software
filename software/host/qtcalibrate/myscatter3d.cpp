#include <QWidget>
#include "myscatter3d.h"
#include <QDebug>
#include <QVBoxLayout>
#include <QtDataVisualization/QValue3DAxis>



MyScatter3D::MyScatter3D(QWidget *parent)
    : QWidget{parent}
{
    QVBoxLayout *layout = new QVBoxLayout();
    setLayout(layout);

    graph = new Q3DScatter();
    series = new QScatter3DSeries;


    graph->setAspectRatio(1.0);
    graph->setHorizontalAspectRatio(1.0); 

    series->setItemSize(0.1f);

    QWidget *container = QWidget::createWindowContainer(graph);
    layout->addWidget(container, 1);

    // 2. Create and configure a QValue3DAxis for each dimension
    QValue3DAxis *axisX = new QValue3DAxis;
    QValue3DAxis *axisY = new QValue3DAxis;
    QValue3DAxis *axisZ = new QValue3DAxis;

    // Set the title for each axis
    axisX->setTitle("mX");
    axisY->setTitle("mY");
    axisZ->setTitle("mZ");

    

    // Make the titles visible
    axisX->setTitleVisible(true);
    axisY->setTitleVisible(true);
    axisZ->setTitleVisible(true);

    // turn off axis numbers

    axisX->setLabelFormat("");
    axisY->setLabelFormat("");
    axisZ->setLabelFormat("");
    
    // Set ranges for the axes
    //axisX->setRange(0, 10);
    //axisY->setRange(0, 10);
    //axisZ->setRange(0, 10);

    // 3. Assign the new axes to the graph
    graph->setAxisX(axisX);
    graph->setAxisY(axisY);
    graph->setAxisZ(axisZ);

    //graph->activeTheme()->setType(Q3DTheme::ThemePrimaryColors);
    //graph->activeTheme()->setBaseColor(Qt::red);

    graph->activeTheme()->setType(Q3DTheme::ThemePrimaryColors);

    QColor transparentBlue(0, 0, 255, 64);
 
    series->setBaseColor(transparentBlue);
    
    QScatterDataArray data;
    data << QVector3D(0.5f, 0.5f, 0.5f) << QVector3D(-0.3f, -0.5f, -0.4f) << QVector3D(0.0f, -0.3f, 0.2f);
    
    series->dataProxy()->addItems(data);
    graph->addSeries(series);
}

MyScatter3D::~MyScatter3D()
{
    delete graph;
    delete series;
}

void MyScatter3D::addData(float x, float y, float z){
    series->dataProxy()->addItem(QScatterDataItem(QVector3D(x,y,z)));
}
