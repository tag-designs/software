#ifndef SENSORVIZ_SENSORPROFILE_H
#define SENSORVIZ_SENSORPROFILE_H

#include <QColor>
#include <QString>
#include <QStringList>
#include <QVector>

#include "sensorstream.h"

// Definitions describe how to read a family of SQLite logs. They are deliberately
// separate from loaded data so support for new tags or new tables can be added
// without editing MainWindow or the plotting code.

struct ScalarStreamDefinition
{
    QString id;
    QString label;
    QString units;
    QString table;
    QString valueColumn;
    QColor color;
    bool defaultVisible = true;
    SensorAxisSide axisSide = SensorAxisSide::Left;
    SensorAxisRange axisRange;
};

struct RecordSetDefinition
{
    QString id;
    QString label;
    QString table;
    QStringList valueColumns;
};

struct TransformParameterDefinition
{
    QString id;
    QString label;
    QString units;
    double defaultValue = 0.0;
    double minimum = 0.0;
    double maximum = 0.0;
    int decimals = 0;
};

struct SensorTransformDefinition
{
    QString id;
    QString label;
    QString outputStreamId;
    QStringList requiredStreamIds;
    QStringList requiredRecordSetIds;
    QVector<TransformParameterDefinition> parameters;
};

struct SensorProfile
{
    QString tagType;
    QString displayName;
    QVector<ScalarStreamDefinition> scalarStreams;
    QVector<RecordSetDefinition> recordSets;
    QVector<SensorTransformDefinition> transforms;
};

// Return the best-known profile for a tag type. The current profiles share a
// common table catalog, but this gives future compass/IMU tags a place to add
// record sets, transforms, and specialized defaults.
SensorProfile sensorProfileForTag(const QString &tagType);

#endif
