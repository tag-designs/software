#include <QtCore>
#include <QtGui>
#include <QDebug>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QVector>
#include <qcustomplot.h>

#include <cmath>
#include <sqlite3.h>
#include <stdexcept>

#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "../qtfiledialog.h"

namespace
{

class SqliteDatabase
{
public:
    explicit SqliteDatabase(const QString &path)
    {
        const QByteArray utf8_path = path.toUtf8();
        if (sqlite3_open_v2(
                utf8_path.constData(), &db_, SQLITE_OPEN_READONLY, nullptr)
            != SQLITE_OK) {
            last_error_ = errorString();
            close();
        }
    }

    ~SqliteDatabase()
    {
        close();
    }

    SqliteDatabase(const SqliteDatabase &) = delete;
    SqliteDatabase &operator=(const SqliteDatabase &) = delete;

    bool isOpen() const
    {
        return db_ != nullptr;
    }

    sqlite3 *get() const
    {
        return db_;
    }

    QString lastError() const
    {
        if (!last_error_.isEmpty()) {
            return last_error_;
        }
        return errorString();
    }

private:
    QString errorString() const
    {
        return QString::fromUtf8(db_ ? sqlite3_errmsg(db_) : "database is not open");
    }

    void close()
    {
        if (db_) {
            sqlite3_close(db_);
            db_ = nullptr;
        }
    }

    sqlite3 *db_ = nullptr;
    QString last_error_;
};

class SqliteStatement
{
public:
    SqliteStatement(SqliteDatabase &db, const char *sql) : db_(db.get())
    {
        if (sqlite3_prepare_v2(db_, sql, -1, &stmt_, nullptr) != SQLITE_OK) {
            last_error_ = errorString();
            stmt_ = nullptr;
        }
    }

    ~SqliteStatement()
    {
        if (stmt_) {
            sqlite3_finalize(stmt_);
        }
    }

    SqliteStatement(const SqliteStatement &) = delete;
    SqliteStatement &operator=(const SqliteStatement &) = delete;

    bool valid() const
    {
        return stmt_ != nullptr;
    }

    bool next()
    {
        const int rc = sqlite3_step(stmt_);
        if (rc == SQLITE_ROW) {
            return true;
        }
        if (rc != SQLITE_DONE) {
            last_error_ = errorString();
        }
        return false;
    }

    qint64 int64Column(int column) const
    {
        return sqlite3_column_int64(stmt_, column);
    }

    double doubleColumn(int column) const
    {
        return sqlite3_column_double(stmt_, column);
    }

    QString textColumn(int column) const
    {
        const unsigned char *text = sqlite3_column_text(stmt_, column);
        return QString::fromUtf8(reinterpret_cast<const char *>(text ? text : (const unsigned char *)""));
    }

    QString lastError() const
    {
        if (!last_error_.isEmpty()) {
            return last_error_;
        }
        return errorString();
    }

private:
    QString errorString() const
    {
        return QString::fromUtf8(db_ ? sqlite3_errmsg(db_) : "database is not open");
    }

    sqlite3 *db_ = nullptr;
    sqlite3_stmt *stmt_ = nullptr;
    QString last_error_;
};

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

    // Load calibration data -- use last entry in table

    SqliteStatement calibration_query(
        db,
        "SELECT Epoch, Constants FROM Calibration ORDER BY Epoch DESC LIMIT 1");
    if (!calibration_query.valid()) {
        msgBox.setText("Warning: no calibration constants: " + calibration_query.lastError());
        msgBox.exec();
    } else if (calibration_query.next()) {
        QJsonObject constants =
            QJsonDocument::fromJson(calibration_query.textColumn(1).toUtf8()).object();
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
        qDebug() << "calibration timestamp:" << calibration_query.int64Column(0);
    } else {
        msgBox.setText("Warning: no calibration constants");
        msgBox.exec();
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

    // load compass data

    SqliteStatement compass_query(db, "SELECT Epoch, ax, ay, az, mx, my, mz FROM Compass");
    if (!requireStatement(msgBox, compass_query, "Failed to load compass data")) {
        return;
    }
    while (compass_query.next()) {
        sensor s;
        qint64 timestamp = compass_query.int64Column(0);
        
        // load accelerometer and magnetometer raw data
        s.accel.setX(compass_query.doubleColumn(1));
        s.accel.setY(compass_query.doubleColumn(2));
        s.accel.setZ(compass_query.doubleColumn(3));
        s.mag.setX(compass_query.doubleColumn(4));
        s.mag.setY(compass_query.doubleColumn(5));
        s.mag.setZ(compass_query.doubleColumn(6));
        
        // compute orientation quaternion
        eCompass(s.mag, s.accel, s.q, s.dip, s.field, s.mg);
        
        // save orientation pitch, roll, yaw
        QVector3D angles = s.q.toEulerAngles();
    
        if (angles[2] < 0.0) angles[2] += 360.0;
        s.pitch = angles[0];
        s.roll = angles[1];
        s.yaw = std::fmod(360 - angles[2], 360.0);
        orientation_time << timestamp;
        // save the sensor data and computed orientation
        orientation << s;
        accel << s.mg;  // save total acceleration
        // store the heading for graph
        heading << std::fmod(720 + s.yaw + declination, 360.0);
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

    temperatureGraph->setData(temperature_time, temperature, true);
    voltageGraph->setData(voltage_time, voltage, true);
    activityGraph->setData(activity_time, activity, true);
    headingGraph->setData(orientation_time, heading, true);
    accelGraph->setData(orientation_time, accel, true);
    //accelGraph->setVisible(false);

    left->start->setCoords(ui->plot->xAxis->range().lower, QCPRange::minRange);
    left->end->setCoords(ui->plot->xAxis->range().lower, QCPRange::maxRange);
    right->start->setCoords(ui->plot->xAxis->range().upper, QCPRange::minRange);
    right->end->setCoords(ui->plot->xAxis->range().upper, QCPRange::maxRange);
    ui->plot->replot();
}
