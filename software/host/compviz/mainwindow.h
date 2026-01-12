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
    void on_actionReset_triggered(){};
    void on_actionPrint_triggered();


    // helper

    // toggle visibility

    void on_cb_activity_clicked(bool checked);

    // data filters

    void on_cb_filter_low_pass_toggled(bool checked);
    void on_sb_cutoff_valueChanged(double freq);

    // plotting

    void on_offsetUTC_valueChanged(int hours);

    // export

    void renderPlot(QPrinter*);

    // cursors

    void on_pb_cursor_zoom_clicked();
    void on_cb_cursors_visible_clicked(bool checked);
    void plot_doubleclick(QMouseEvent *event);

    // mouse move

    void onMouseMove(QMouseEvent* event);

   
    void on_gbVT_clicked();
    void on_rbVoltage_clicked();
    void on_rbTemperature_clicked();
    

private:

    // ecompass

    bool eCompass(QVector3D mag, QVector3D accel, QQuaternion &q, 
                  float& dip, float& field, float& mg); 

    void apply_calibration(QVector3D &mag);  
    
    float Vcal[3];
    float Acal[3][3];

    // gui state

      
    enum TagType {BITTAG, BITTAGNG, PRESTAG, COMPASSTAG};
    TagType tagtype;

    Ui::MainWindow *ui;
    QActionGroup* vt_group;
    QVector<double> accel_time;
    QVector<double> accel;
    QVector<double> accel_time_filtered;
    QVector<double> accel_filtered;
    QVector<double> voltage_time;
    QVector<double> temperature_time;
    QVector<double> voltage;
    QVector<double> temperature;
    QVector<sensor> orientation;
    QVector<double> orientation_time;
    QString path;
    QCPItemLine *left;
    QCPItemLine *right;
    QString currentfilename;
    void makeVisible(bool);
    QCPItemText *textItem;
    QSharedPointer<QCPAxisTickerDateTime> dateTicker;

};

#endif // MAINWINDOW_H
