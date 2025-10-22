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
    //graph->setMaxCameraYRotation(180.0);

    //graph->setShadowQuality(QtGraphs3D::ShadowQuality::SoftMedium);


    graph->setAspectRatio(1.0);
    graph->setHorizontalAspectRatio(1.0); 

    // connect(graph, &Q3DScatterWidgetItem::cameraXRotationChanged, this, &MyScatter3D::cameraXRotationChanged);

    

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
    // hide grid lines
    axisX->setSegmentCount(0);
    axisY->setSegmentCount(0);
    axisZ->setSegmentCount(0);
    

    maxRange = 65.0;
    graph->setAxisX(axisX);
    graph->setAxisY(axisY);
    graph->setAxisZ(axisZ);

    //QColor transparentBlue(0, 0, 255, 156);

    series = new QScatter3DSeries();
    series->setItemSize(0.02f);
 
    series->setBaseColor(Qt::blue);

    

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
    originSeries->setItemSize(0.03f); // Example: adjust the size

    // Create series for for sphere

    //QLinearGradient gradient(-60,-60,60,60);
    //gradient.setColorAt(0.0, QColor(212, 20, 26));
    //gradient.setColorAt(1.0, QColor(97, 20, 212));//Qt::green);

    sphereSeries = new QScatter3DSeries;

    //sphereSeries->setBaseGradient(gradient);
    //sphereSeries->setColorStyle(QGraphsTheme::ColorStyle::RangeGradient);
    
    QColor transparentGreen(0, 255, 0, 128);
    sphereSeries->setBaseColor(transparentGreen);
    sphereSeries->setItemSize(0.02f);


    // missing regions

    regionSeries = new QScatter3DSeries;
    regionSeries->setBaseColor(Qt::black); 
    regionSeries->setItemSize(0.01f); 
    
    
    // add series to graph
  
    graph->addSeries(series);
    graph->addSeries(originSeries);
    graph->addSeries(sphereSeries);
    //graph->addSeries(regionSeries);

    // Disable the background
    QGraphsTheme *theme = graph->activeTheme();
    theme->setBackgroundVisible(false);
    theme->setGridVisible(false);
    theme->setLabelBackgroundVisible(false);
    theme->setLabelBorderVisible(false);
    theme->setPlotAreaBackgroundVisible(false);

}

MyScatter3D::~MyScatter3D()
{
    //delete graph;
    //delete series;
}


void MyScatter3D::addData(float x, float y, float z){
    QScatterDataItem item(x,y,z);
    series->dataProxy()->addItem(item);
    originSeries->dataProxy()->setItem(1,item);
    adjustRange(abs(x));
    adjustRange(abs(y));
    adjustRange(abs(z));
    // modify camera

    //return;

    float pitch, yaw, roll;

    // v is point to track -- convert to up y

    QVector3D v = QVector3D(x,y,z);
    v.normalize();

    // camera vector
   
    QVector3D camera = QVector3D(0,0,-1);  // camera along z axis

    // compute rotation

    QQuaternion rotation = QQuaternion::rotationTo(camera,v);
    rotation.getEulerAngles(&pitch,&yaw,&roll);

    // set camera rotation
    //  50,0,0 has yaw of -90. -- (0,-90,0)
    //.  0,0,0 as pitch of 90.  -- (90,0,0)
    //. 30,30,0                 --  (45,-90,45)

    graph->setCameraXRotation(yaw);
    graph->setCameraYRotation(pitch);
 
   
}


#define MAX(a,b) a > b ? a : b
void MyScatter3D::setData(QScatterDataArray& data)
{
    clearData();
    series->dataProxy()->addItems(data);
    float maxval = 0.0;
    for (QScatterDataItem item : data){
        maxval = MAX(maxval,abs(item.x()));
        maxval = MAX(maxval,abs(item.y()));
        maxval = MAX(maxval,abs(item.z()));
    } 
    adjustRange(maxval);
}

void MyScatter3D::setRegionData(QScatterDataArray &data){
    //regionSeries->dataProxy()->resetArray();
    //regionSeries->dataProxy()->addItems(data);
}


void MyScatter3D::clearData(){
    series->dataProxy()->resetArray();
    maxRange = 0.0;
    adjustRange(sphereRange);//resetArray(new QScatterDataArray());
}

void MyScatter3D::drawSphere(float radius){
    sphereSeries->dataProxy()->resetArray();
    for (int i = 0; i < 360; i += 9){
        for (int j = 0; j < 360; j+= 9) {
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


void MyScatter3D::cameraXRotationChanged(float rotation){
    float pitch,yaw,row;
    QScatterDataItem item = originSeries->dataProxy()->itemAt(1);
    QQuaternion q = QQuaternion(1.0,item.position());

    q.getEulerAngles(&pitch,&yaw,&row);
    qInfo() << "target changed: " << graph->cameraXRotation() << graph->cameraYRotation();
}