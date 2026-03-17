#include <QtCore>
#include <QtSql>
#include <QtSql/QSqlDatabase>
#include <QtGui>
#include <QVector>
#include <QDebug>
#include <qcustomplot.h>
#include <stdexcept>
#include <float.h>

#include "mainwindow.h"
#include "ui_mainwindow.h"

void MainWindow::on_pb_load_clicked()
{
    QMessageBox msgBox;
    QSqlDatabase db =  QSqlDatabase::addDatabase("QSQLITE");
    QSqlQuery query(db);

    // load database

    QString fileName = QFileDialog::getOpenFileName(this, tr("Open Data"), path,
                                                    tr("Data Files (*.db3)"));
    
    if (fileName.isNull())
        return;

    db.setDatabaseName(fileName);
    if (db.open()){
        qDebug() << "Database connected successfully";
    } else {
        qDebug() << "Couldn't open database file " << fileName <<  " error: " << db.lastError().text();
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

    query.exec("SELECT fieldname, value FROM Info");
    qDebug() << "loading info: " << db.lastError().text();
   
    while (query.next()){
        QString fieldname = query.value(0).toString();
        QString value = query.value(1).toString();

        qDebug() << fieldname;

        // check tag type

        if (fieldname == "tagtype") {
                if (value != "COMPASSTAG"){
                    msgBox.setText("unexpected tag type: " + value);
                    msgBox.exec();
                    return;
                }
            }
    }

    // Load calibration data -- use last entry in table

    if (!query.exec("SELECT Epoch, Constants FROM Calibration ORDER BY Epoch DESC LIMIT 1")){
        msgBox.setText("Warning: no calibration constants:" + db.lastError().text());
        msgBox.exec();
    }

    if (query.next()){
        QJsonObject constants = QJsonDocument::fromJson(query.value("Constants").toString().toUtf8()).object();
        constants = constants["magnetometer"].toObject();
        Hcal[0]    = constants["v0"].toDouble();
        Hcal[1]    = constants["v1"].toDouble();
        Hcal[2]    = constants["v2"].toDouble();
        Scal[0][0] = constants["a00"].toDouble();
        Scal[0][1] = constants["a01"].toDouble();
        Scal[0][2] = constants["a02"].toDouble();
        Scal[1][0] = constants["a10"].toDouble();
        Scal[1][1] = constants["a11"].toDouble();
        Scal[1][2] = constants["a12"].toDouble();
        Scal[2][0] = constants["a20"].toDouble();
        Scal[2][1] = constants["a21"].toDouble();
        Scal[2][2] = constants["a22"].toDouble();
        qDebug() << "calibration timestamp:" << query.value("Epoch").toLongLong();
    } else {
        msgBox.setText("Warning: no calibration constants:" + db.lastError().text());
        msgBox.exec();
    }
    

    // load Core Temperature

    query.exec("SELECT Epoch, Temperature FROM CoreTemperature");
    while (query.next()){
        qint64 timestamp = query.value(0).toLongLong();
        double value = query.value(1).toDouble();

        temperature_time << timestamp;
        temperature << value;
    }

    // load voltage

    query.exec("SELECT Epoch, Voltage FROM Voltage");
    while (query.next()){
        qint64 timestamp = query.value(0).toLongLong();
        double value = query.value(1).toDouble();

        voltage_time << timestamp;
        voltage << value;
    }

    // load activty

    query.exec("SELECT Epoch, Activity FROM Activity");
    while (query.next()){
        qint64 timestamp = query.value(0).toLongLong();
        double value = query.value(1).toDouble();

        activity_time << timestamp;
        activity << value;
    }

    // load compass data

    query.exec("SELECT Epoch, ax, ay, az, mx, my, mz FROM Compass");
    while (query.next()){
        sensor s;
        qint64 timestamp = query.value(0).toLongLong();
        
        // load accelerometer and magnetometer raw data
        s.accel.setX(query.value(1).toDouble());
        s.accel.setY(query.value(2).toDouble());
        s.accel.setZ(query.value(3).toDouble());
        s.mag.setX(query.value(4).toDouble());
        s.mag.setY(query.value(5).toDouble());
        s.mag.setZ(query.value(6).toDouble());
        
        // compute orientation quaternion
        eCompass(s.mag,s.accel,s.q,s.dip,s.field,s.mg);
        
        // save orientation pitch, roll, yaw
        QVector3D angles = s.q.toEulerAngles();
    
	    if (angles[2] < 0.0) angles[2] += 360.0;
        s.pitch = angles[0];
        s.roll = angles[1];
        s.yaw = angles[2];
        orientation_time << timestamp;
        // save the sensor data and computed orientation
        orientation << s;
        accel << s.mg;  // save total acceleration
        // store the heading for graph
        heading << std::fmod(s.yaw + declination,360.0);
        //qDebug() << s.yaw << std::fmod(s.yaw + declination,360.0) << declination;
    }

    if (activity_time.size())
    {
        // reset cursors
        double size = activity_time.size();

        // set range

        ui->plot->xAxis->setRange(activity_time[0], activity_time[size - 1]);
        ui->plot->xAxis->scaleRange(1.1, ui->plot->xAxis->range().center()); // 10% margin
    }

    temperatureGraph->setData(temperature_time,temperature,true);
    voltageGraph->setData(voltage_time,voltage,true);
    activityGraph->setData(activity_time,activity,true);
    headingGraph->setData(orientation_time,heading,true);
    accelGraph->setData(orientation_time, accel,true);
    //accelGraph->setVisible(false);

    left->start->setCoords(ui->plot->xAxis->range().lower, QCPRange::minRange);
    left->end->setCoords(ui->plot->xAxis->range().lower, QCPRange::maxRange);
    right->start->setCoords(ui->plot->xAxis->range().upper, QCPRange::minRange);
    right->end->setCoords(ui->plot->xAxis->range().upper, QCPRange::maxRange);
    ui->plot->replot();
}
