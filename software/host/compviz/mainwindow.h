#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QVector>
#include <QPrinter>
#include <qcustomplot.h>
#include<QQuickItem>
#include <QVector3D>
#include <QQuaternion>
#include <QActionGroup>

typedef struct {
    QVector3D mag, accel;
    QQuaternion q;
    float dip, field, mg;
    float pitch,roll,yaw;
} sensor;

namespace Ui {
class MainWindow;
}

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:


    // actions

    void on_pb_redraw_clicked();
    void on_pb_load_clicked();
    void on_actionAbout_triggered();
    void on_actionLoad_triggered(){ on_pb_load_clicked();};
    void on_actionReset_triggered();
    void on_actionPrint_triggered();
    void on_actionActivity_triggered(bool checked = false);
    void on_actionHeading_triggered(bool checked = false);
    void on_actionVoltage_triggered(bool checked = false);
    void on_actionTemperature_triggered(bool checked = false);
    void on_actionCompass_Declination();
    void on_actionBattery_Forward_triggered(bool checked = false);
    void on_actionUTC_Offset_triggered();
    void on_actionCalibration_Constants_triggered();
    //void on_actionEnable_Filter_triggered(bool checked = false);

    // context menus

    void showPlotContextMenu(QPoint pos);
    void showCompassContextMenu(QPoint pos);


    // helper

    // toggle visibility

    void on_cb_activity_clicked(bool checked);

    // export

    void renderPlot(QPrinter*);

    // mouse move

    void onMouseMove(QMouseEvent* event);
    

private:

    void createGraphs();

    // ecompass

    bool eCompass(QVector3D mag, QVector3D accel, QQuaternion &q, 
                  float& dip, float& field, float& mg); 

    void apply_calibration(QVector3D &mag);  
    void updateHeadingGraph();
    
    float Vcal[3];
    float Acal[3][3] = {1.0,0.0,0.0,0.0,1.0,0.0,0.0,0.0,1.0};

    float declination;
    int utc_offset = 0;

    // gui state

      
    enum TagType {BITTAG, BITTAGNG, PRESTAG, COMPASSTAG};
    TagType tagtype;

    QString path;
    QString currentfilename;
    void makeVisible(bool);

    Ui::MainWindow *ui;
    QQuickItem *rootObject;

    QActionGroup* vt_group;

    // data 
    QVector<double> accel_time;
    QVector<double> accel;
    QVector<double> voltage_time;
    QVector<double> temperature_time;
    QVector<double> voltage;
    QVector<double> temperature;
    QVector<sensor> orientation;
    QVector<double> heading;
    QVector<double> orientation_time;

    // custom plot
   
    QCPItemLine *left;
    QCPItemLine *right;

    //QCPItemText *textItem;
    QSharedPointer<QCPAxisTickerDateTime> dateTicker;

    QCPGraph *activityGraph;
    QCPGraph *temperatureGraph;
    QCPGraph *voltageGraph;
    QCPGraph *headingGraph;

    QCPAxis *activityAxis;
    QCPAxis *temperatureAxis;
    QCPAxis *voltageAxis;
    QCPAxis *headingAxis;

};

#endif // MAINWINDOW_H
