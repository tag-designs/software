#include <QtCore>
#include <QtGui>
#include <QDebug>
#include <QMessageBox>
#include <QVector>
#include <qcustomplot.h>

#include <cmath>

#include "compass_sqlite_loader.h"
#include "mainwindow.h"
#include "sqlite_utils.h"
#include "ui_mainwindow.h"
#include "../qtfiledialog.h"

namespace
{

bool requireStatement(QMessageBox &msgBox, SqliteStatement &stmt, const QString &context)
{
    if (stmt.valid()) {
        return true;
    }

    msgBox.setText(context + ": " + stmt.lastError());
    msgBox.exec();
    return false;
}

} // namespace

void MainWindow::on_pb_load_clicked()
{
    QMessageBox msgBox;

    // load database

    QString fileName = HostFileDialog::getOpenFileName(
        this, tr("Open Data"), path, tr("Data Files (*.db3);;All Files (*)"));
    
    if (fileName.isNull())
        return;

    SqliteDatabase db(fileName);
    if (db.isOpen()) {
        qDebug() << "Database connected successfully";
    } else {
        qDebug() << "Couldn't open database file " << fileName << " error: " << db.lastError();
        return;
    }

    // clear data vectors

    voltage.clear();
    voltage_time.clear();
    temperature_time.clear();
    temperature.clear();
    activity_time.clear();
    activity.clear();
    accel.clear();
    orientation.clear();
    orientation_time.clear();
    heading.clear();

    // clear reset UI

    activityGraph->data()->clear();
    temperatureGraph->data()->clear();
    voltageGraph->data()->clear();
    headingGraph->data()->clear();
    accelGraph->data()->clear();
    ui->te_fileinfo->clear();
    ui->te_fileinfo->append(fileName);

    // Load information table

    SqliteStatement info_query(db, "SELECT fieldname, value FROM Info");
    if (!requireStatement(msgBox, info_query, "Failed to load info")) {
        return;
    }

    while (info_query.next()) {
        QString fieldname = info_query.textColumn(0);
        QString value = info_query.textColumn(1);

        qDebug() << fieldname;

        // check tag type

        if (fieldname == "tagtype") {
            if (value != "COMPASSTAG") {
                msgBox.setText("unexpected tag type: " + value);
                msgBox.exec();
                return;
            }
        }
    }

    // load Core Temperature

    SqliteStatement temperature_query(db, "SELECT Epoch, Temperature FROM CoreTemperature");
    if (!requireStatement(msgBox, temperature_query, "Failed to load core temperature")) {
        return;
    }
    while (temperature_query.next()) {
        qint64 timestamp = temperature_query.int64Column(0);
        double value = temperature_query.doubleColumn(1);

        temperature_time << timestamp;
        temperature << value;
    }

    // load voltage

    SqliteStatement voltage_query(db, "SELECT Epoch, Voltage FROM Voltage");
    if (!requireStatement(msgBox, voltage_query, "Failed to load voltage")) {
        return;
    }
    while (voltage_query.next()) {
        qint64 timestamp = voltage_query.int64Column(0);
        double value = voltage_query.doubleColumn(1);

        voltage_time << timestamp;
        voltage << value;
    }

    // load activity

    SqliteStatement activity_query(db, "SELECT Epoch, Activity FROM Activity");
    if (!requireStatement(msgBox, activity_query, "Failed to load activity")) {
        return;
    }
    while (activity_query.next()) {
        qint64 timestamp = activity_query.int64Column(0);
        double value = activity_query.doubleColumn(1);

        activity_time << timestamp;
        activity << value;
    }

    // Load compass data. The compass loader owns calibration parsing and the
    // eCompass math so this UI path only has to store returned data and plot it.

    CompassLogData compassLog;
    QString compassWarning;
    QString compassError;
    if (!loadCompassData(db, compassLog, compassWarning, compassError)) {
        msgBox.setText(compassError);
        msgBox.exec();
        return;
    }

    calibration = compassLog.calibration;
    orientation = compassLog.orientation;
    orientation_time = compassLog.orientationTime;
    accel = compassLog.acceleration;
    heading.resize(orientation.size());
    updateHeadingGraph();

    if (!compassWarning.isEmpty()) {
        msgBox.setText(compassWarning);
        msgBox.exec();
    }

    if (activity_time.size())
    {
        // reset cursors
        double size = activity_time.size();

        // set range

        ui->plot->xAxis->setRange(activity_time[0], activity_time[size - 1]);
        ui->plot->xAxis->scaleRange(1.1, ui->plot->xAxis->range().center()); // 10% margin
    }

    temperatureGraph->setData(temperature_time, temperature, true);
    voltageGraph->setData(voltage_time, voltage, true);
    activityGraph->setData(activity_time, activity, true);
    accelGraph->setData(orientation_time, accel, true);
    //accelGraph->setVisible(false);

    left->start->setCoords(ui->plot->xAxis->range().lower, QCPRange::minRange);
    left->end->setCoords(ui->plot->xAxis->range().lower, QCPRange::maxRange);
    right->start->setCoords(ui->plot->xAxis->range().upper, QCPRange::minRange);
    right->end->setCoords(ui->plot->xAxis->range().upper, QCPRange::maxRange);
    ui->plot->replot();
}
