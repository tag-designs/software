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
#include "qtfiledialog.h"

namespace
{

// Convert statement-preparation failures into the same modal error style used
// by the rest of this older UI path.
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

    // All vectors and graphs below are MainWindow-owned. The compass-specific
    // parsing/math is delegated, but this function still coordinates the whole
    // "choose a file and display it" workflow.

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

    // Clear all old data before validating the new file. If the new load fails,
    // the UI should not leave stale plots from the previous log.
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

    activityGraph->data()->clear();
    temperatureGraph->data()->clear();
    voltageGraph->data()->clear();
    headingGraph->data()->clear();
    accelGraph->data()->clear();
    ui->te_fileinfo->clear();
    ui->te_fileinfo->append(fileName);

    // The CompViz UI currently only understands CompassTag schemas. Other tag
    // types should be routed through SensorViz or a future shared viewer.

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

    // Scalar streams are simple time/value tables, so they remain local to this
    // UI loader. The compass table is richer and is handled below by a helper.

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

    // Heading is not loaded from the file. It is derived from magnetic yaw plus
    // current display settings, and must be rebuilt when those settings change.
    heading.resize(orientation.size());
    updateHeadingGraph();

    if (!compassWarning.isEmpty()) {
        msgBox.setText(compassWarning);
        msgBox.exec();
    }

    if (activity_time.size())
    {
        double size = activity_time.size();

        // Use activity timestamps as the main x range because activity exists
        // in all current CompassTag logs.
        ui->plot->xAxis->setRange(activity_time[0], activity_time[size - 1]);
        ui->plot->xAxis->scaleRange(1.1, ui->plot->xAxis->range().center()); // 10% margin
    }

    temperatureGraph->setData(temperature_time, temperature, true);
    voltageGraph->setData(voltage_time, voltage, true);
    activityGraph->setData(activity_time, activity, true);
    accelGraph->setData(orientation_time, accel, true);
    // Initialize the zoom cursors to bracket the loaded time range.
    left->start->setCoords(ui->plot->xAxis->range().lower, QCPRange::minRange);
    left->end->setCoords(ui->plot->xAxis->range().lower, QCPRange::maxRange);
    right->start->setCoords(ui->plot->xAxis->range().upper, QCPRange::minRange);
    right->end->setCoords(ui->plot->xAxis->range().upper, QCPRange::maxRange);
    ui->plot->replot();
}
