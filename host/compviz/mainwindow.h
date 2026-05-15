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

#include "compass_display.h"
#include "compass_types.h"

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
    void on_actionAcceleration_triggered(bool checked = false);
    void on_actionVoltage_triggered(bool checked = false);
    void on_actionTemperature_triggered(bool checked = false);
    void on_actionCompass_Declination();
    void on_actionBattery_Forward_triggered(bool checked = false);
    void on_actionUTC_Offset_triggered();
    void on_actionCalibration_Constants_triggered();
    void on_actionZoom_to_Cursors_triggered();
    //void on_actionEnable_Filter_triggered(bool checked = false);

    // context menus

    void showPlotContextMenu(QPoint pos);
    void showCompassContextMenu(QPoint pos);


    // cursors

    void plot_doubleclick(QMouseEvent *event);

    // toggle visibility

    void on_cb_activity_clicked(bool checked);

    // export

    void renderPlot(QPrinter*);

    // mouse move

    void onMouseMove(QMouseEvent* event);
    

private:

    void createGraphs();

    void updateHeadingGraph();

    // Current hard/soft iron constants displayed by the calibration dialog
    // and used by the compass loader when deriving orientation samples.
    CompassCalibration calibration;

    float declination = 0.0;
    int utc_offset = 0;

    // gui state

      
    enum TagType {BITTAG, BITTAGNG, PRESTAG, COMPASSTAG};
    TagType tagtype;

    QString path;
    QString currentfilename;
    void makeVisible(bool);

    Ui::MainWindow *ui;
    CompassDisplay compassDisplay;

    QActionGroup* vt_group;
    QActionGroup* aa_group;

    // data 
    QVector<double> activity_time;
    QVector<double> activity;
    QVector<double> voltage_time;
    QVector<double> temperature_time;
    QVector<double> voltage;
    QVector<double> temperature;
    QVector<CompassDerivedSample> orientation;
    QVector<double> heading;
    QVector<double> orientation_time;
    QVector<double> accel;  // use orientation_time

    // custom plot
   
    QCPItemLine *left;
    QCPItemLine *right;

    //QCPItemText *textItem;
    QSharedPointer<QCPAxisTickerDateTime> dateTicker;

    QCPGraph *activityGraph;
    QCPGraph *temperatureGraph;
    QCPGraph *voltageGraph;
    QCPGraph *headingGraph;
    QCPGraph *accelGraph;

    QCPAxis *activityAxis;
    QCPAxis *temperatureAxis;
    QCPAxis *voltageAxis;
    QCPAxis *headingAxis;
    QCPAxis *accelAxis;

};

#endif // MAINWINDOW_H
