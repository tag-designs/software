#include "compass_sqlite_loader.h"

#include "compass_processor.h"
#include "sqlite_utils.h"

#include <QDebug>
#include <QJsonDocument>
#include <QJsonObject>

namespace
{

bool loadCalibration(SqliteDatabase &db, CompassCalibration &calibration, QString &warning)
{
    SqliteStatement calibrationQuery(
        db,
        "SELECT Epoch, Constants FROM Calibration ORDER BY Epoch DESC LIMIT 1");

    if (!calibrationQuery.valid()) {
        warning = "Warning: no calibration constants: " + calibrationQuery.lastError();
        return false;
    }

    if (!calibrationQuery.next()) {
        warning = "Warning: no calibration constants";
        return false;
    }

    QJsonObject constants =
        QJsonDocument::fromJson(calibrationQuery.textColumn(1).toUtf8()).object();
    constants = constants["magnetometer"].toObject();
    calibration = CompassCalibration::fromMagnetometerJson(constants);

    qDebug() << "calibration timestamp:" << calibrationQuery.int64Column(0);
    return true;
}

} // namespace

bool loadCompassData(
    SqliteDatabase &db,
    CompassLogData &log,
    QString &warning,
    QString &error)
{
    warning.clear();
    error.clear();
    log = CompassLogData();

    loadCalibration(db, log.calibration, warning);

    SqliteStatement compassQuery(db, "SELECT Epoch, ax, ay, az, mx, my, mz FROM Compass");
    if (!compassQuery.valid()) {
        error = "Failed to load compass data: " + compassQuery.lastError();
        return false;
    }

    CompassProcessor processor(log.calibration);
    while (compassQuery.next()) {
        CompassRawSample raw;
        raw.epoch = compassQuery.int64Column(0);
        raw.accel.setX(compassQuery.doubleColumn(1));
        raw.accel.setY(compassQuery.doubleColumn(2));
        raw.accel.setZ(compassQuery.doubleColumn(3));
        raw.mag.setX(compassQuery.doubleColumn(4));
        raw.mag.setY(compassQuery.doubleColumn(5));
        raw.mag.setZ(compassQuery.doubleColumn(6));

        CompassDerivedSample sample = processor.deriveSample(raw);
        log.orientationTime << sample.epoch;
        log.orientation << sample;
        log.acceleration << sample.mg;
    }

    return true;
}
