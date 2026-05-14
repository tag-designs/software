#ifndef SENSORVIZ_SENSORSTREAM_H
#define SENSORVIZ_SENSORSTREAM_H

#include <QColor>
#include <QMap>
#include <QVector>
#include <QString>

struct SensorStream
{
    QString id;
    QString label;
    QString units;
    QVector<double> time;
    QVector<double> value;
    QColor color;
    bool derived = false;
};

struct SensorLog
{
    QString path;
    QString tagType;
    QMap<QString, QString> info;
    QVector<SensorStream> streams;
};

#endif
