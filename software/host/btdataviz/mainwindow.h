#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QVector>
#include <QPrinter>
#include <qcustomplot.h>

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

    // helper

    void activityLevel(QCPRange);

    // toggle visibility

    void on_cb_activity_clicked(bool checked);

    // data filters

    void on_cb_filter_low_pass_toggled(bool checked);
    void on_sb_cutoff_valueChanged(double freq);

    // export

    void on_pb_pdf_clicked();
    void on_pb_png_clicked();
    void on_pb_export_csv_clicked();
    void on_pb_print_clicked();
    void renderPlot(QPrinter*);
    void on_pb_process_clicked();

    // cursors

    void on_pb_cursor_zoom_clicked();
    void on_cb_cursors_visible_clicked(bool checked);
    void plot_doubleclick(QMouseEvent *event);
    void on_te_left_dateTimeChanged(const QDateTime &dateTime);
    void on_te_right_dateTimeChanged(const QDateTime &dateTime);

    // mouse move

    void onMouseMove(QMouseEvent* event);

   
    void on_gbVT_clicked();
    void on_rbVoltage_clicked();
    void on_rbTemperature_clicked();

    // Range

    void on_activityRange_valueChanged(int value);

private:
    Ui::MainWindow *ui;
    QVector<double> accel_time;
    QVector<double> accel_count;
    QVector<double> accel_time_filtered;
    QVector<double> accel_count_filtered;
    QVector<double> voltage_time;
    QVector<double> temperature_time;
    QVector<double> voltage;
    QVector<double> temperature;
    QString path;
    QCPItemLine *left;
    QCPItemLine *right;
    QString currentfilename;
    void makeVisible(bool);
    QCPItemText *textItem;
    QSharedPointer<QCPAxisTickerDateTime> dateTicker;

};

#endif // MAINWINDOW_H
