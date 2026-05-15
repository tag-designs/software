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

    // Rebuild the plotted heading vector from magnetic yaw plus current view
    // settings. Loaded orientation samples are not changed by this operation.
    void updateHeadingGraph();

    // Current hard/soft iron constants displayed by the calibration dialog
    // and used by the compass loader when deriving orientation samples.
    CompassCalibration calibration;

    float declination = 0.0;
    int utc_offset = 0;

    // The loader currently accepts only CompassTag logs, but this enum is kept
    // with the other host tools' tag vocabulary for future shared UI paths.
    enum TagType {BITTAG, BITTAGNG, PRESTAG, COMPASSTAG};
    TagType tagtype;

    QString path;
    QString currentfilename;
    void makeVisible(bool);

    Ui::MainWindow *ui;
    CompassDisplay compassDisplay;

    // View menus expose mutually exclusive choices for the right-side scalar
    // axis and the left-side activity/acceleration axis.
    QActionGroup* vt_group;
    QActionGroup* aa_group;

    // Loaded scalar streams are stored as parallel time/value vectors because
    // QCustomPlot accepts that form directly.
    QVector<double> activity_time;
    QVector<double> activity;
    QVector<double> voltage_time;
    QVector<double> temperature_time;
    QVector<double> voltage;
    QVector<double> temperature;

    // Compass samples are derived once from the log. The heading vector is
    // display-only and is rebuilt when declination or battery direction changes.
    QVector<CompassDerivedSample> orientation;
    QVector<double> heading;
    QVector<double> orientation_time;
    QVector<double> accel;  // use orientation_time

    // Vertical cursor pair used to mark and zoom into a time interval.
    QCPItemLine *left;
    QCPItemLine *right;

    // Shared x-axis ticker; changing UTC offset updates this object in place.
    QSharedPointer<QCPAxisTickerDateTime> dateTicker;

    // One graph/axis pair per displayable stream. Activity and acceleration
    // share the left side by menu convention; temperature and voltage share the
    // right side; heading uses the default left axis.
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
