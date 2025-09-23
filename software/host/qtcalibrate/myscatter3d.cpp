#include <QWidget>
#include "myscatter3d.h"
#include <QDebug>
#include <QVBoxLayout>
#include <QScatterDataItem>
#include <cmath>



//#include <QtDataVisualization/QValue3DAxis>



MyScatter3D::MyScatter3D(QWidget *parent)
    : QWidget{parent}
{
    QVBoxLayout *layout = new QVBoxLayout();
    setLayout(layout);
    QQuickWidget *quickWidget = new QQuickWidget();

    graph = new Q3DScatterWidgetItem();
    graph->setWidget(quickWidget);

    //graph->setShadowQuality(QtGraphs3D::ShadowQuality::SoftMedium);


    graph->setAspectRatio(1.0);
    graph->setHorizontalAspectRatio(1.0); 

    

    //QWidget *container = QWidget::createWindowContainer(graph);
    layout->addWidget(quickWidget, 1);

    // Create and configure a QValue3DAxis for each dimension
    axisX = new QValue3DAxis;
    axisY = new QValue3DAxis;
    axisZ = new QValue3DAxis;

    // Set the title for each axis
    axisX->setTitle("mX");
    axisY->setTitle("mY");
    axisZ->setTitle("mZ");

    // Make the titles visible (invisible)
    axisX->setTitleVisible(false);
    axisY->setTitleVisible(false);
    axisZ->setTitleVisible(false);

    // turn off axis numbers

    axisX->setLabelFormat("");
    axisY->setLabelFormat("");
    axisZ->setLabelFormat("");
    

    // 3. Assign the new axes to the graph
    axisX->setRange(-75.0,75.0);
    axisY->setRange(-75.0,75.0);
    axisZ->setRange(-75.0,75.0);
    maxRange = 65.0;
    graph->setAxisX(axisX);
    graph->setAxisY(axisY);
    graph->setAxisZ(axisZ);

    //QColor transparentBlue(0, 0, 255, 156);

    series = new QScatter3DSeries();
    series->setItemSize(0.01f);
 
    series->setBaseColor(Qt::blue);

    //QLinearGradient gradient(-100,-100,100,100);
    //gradient.setColorAt(0.0, Qt::blue);
    //gradient.setColorAt(1.0, Qt::green);

     // Apply the gradient and set the color style
    //series->setBaseGradient(gradient);
    //series->setColorStyle(QGraphsTheme::ColorStyle::RangeGradient);
    
    //QScatterDataArray data;
    //data << QScatterDataItem(0.5f, 0.5f, 0.5f) << QScatterDataItem(-0.3f, -0.5f, -0.4f) << QScatterDataItem(0.0f, -0.3f, 0.2f);
    //series->dataProxy()->addItems(data);
    // create data at the origin

    originSeries = new QScatter3DSeries;
    QScatterDataItem origin(0.0,0.0,0.0);
    originSeries->dataProxy()->addItem(origin);
    originSeries->dataProxy()->addItem(origin);
    originSeries->setBaseColor(Qt::red); // Example: make the origin red
    originSeries->setItemSize(0.05f); // Example: adjust the size

    // Create series for for sphere

    sphereSeries = new QScatter3DSeries;
    sphereSeries->setBaseColor(Qt::yellow);
    sphereSeries->setItemSize(0.01f);
    
    // add series to graph
  
    graph->addSeries(series);
    graph->addSeries(originSeries);
    graph->addSeries(sphereSeries);

    // Disable the background
    QGraphsTheme *theme = graph->activeTheme();
    theme->setBackgroundVisible(false);
    theme->setGridVisible(true);
    theme->setLabelBackgroundVisible(false);
    theme->setLabelBorderVisible(false);
    //theme->setPlotAreaBackgroundVisible(false);

}

MyScatter3D::~MyScatter3D()
{
    delete graph;
    delete series;
}

void MyScatter3D::addData(float x, float y, float z){
    QScatterDataItem item(x,y,z);
    series->dataProxy()->addItem(item);
    originSeries->dataProxy()->setItem(1,item);
    adjustRange(abs(x));
    adjustRange(abs(y));
    adjustRange(abs(z));
}

void MyScatter3D::clearData(){
    series->dataProxy()->resetArray();
    maxRange = 0.0;
    adjustRange(sphereRange);//resetArray(new QScatterDataArray());
}

void MyScatter3D::drawSphere(float radius){
    sphereSeries->dataProxy()->resetArray();
    for (int i = 0; i < 360; i += 5){
        for (int j = 0; j < 360; j+= 5) {
            float theta = i * (M_PI / 180.0);
            float phi = j * (M_PI /180.0);
            float x = radius*sin(phi)*cos(theta);
            float y = radius*sin(phi)*sin(theta);
            float z = radius*cos(phi);
            QScatterDataItem item(x,y,z);
            sphereSeries->dataProxy()->addItem(item);

        }
    }
    sphereRange = radius;
    adjustRange(sphereRange);

}

void MyScatter3D::adjustRange(float radius){
    if (maxRange < radius){
        maxRange = radius;
        axisX->setRange(-radius-10.0,radius+10.0);
        axisY->setRange(-radius-10.0,radius+10.0);
        axisZ->setRange(-radius-10.0,radius+10.0);
    }
}
