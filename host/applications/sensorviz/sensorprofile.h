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

// Describes one SQLite table that can be loaded directly as a SensorStream.
// sqlite_loader.cpp consumes these definitions, and stream_actions.cpp later
// uses the copied defaults on SensorStream.
struct ScalarStreamDefinition
{
    // Stable stream id used by menus, ranges, transforms, and tests.
    QString id;
    // User-facing label/units.
    QString label;
    QString units;
    // SQLite table and numeric column to query with Epoch.
    QString table;
    QString valueColumn;
    // Default plot appearance.
    QColor color;
    bool defaultVisible = true;
    SensorAxisSide axisSide = SensorAxisSide::Left;
    SensorAxisRange axisRange;
};

// Describes one multi-column SQLite table that is loaded as SensorRecordSet.
// Compass raw samples use this path because one table later produces several
// plotted streams and an orientation display.
struct RecordSetDefinition
{
    QString id;
    QString label;
    QString table;
    QStringList valueColumns;
};

// UI metadata for one parameter of a transform. This is currently used as a
// design target more than a fully generic runtime system; hardcoded transforms
// still use the same vocabulary.
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

// Describes a display transform. SensorProfile keeps this metadata close to
// stream definitions so future UI generation can replace some hardcoded action
// wiring in transforms.cpp and compass_transforms.cpp.
struct SensorTransformDefinition
{
    QString id;
    QString label;
    QString outputStreamId;
    QStringList requiredStreamIds;
    QStringList requiredRecordSetIds;
    QVector<TransformParameterDefinition> parameters;
};

// Catalog for a tag/log family. sensorProfileForTag returns one of these after
// sqlite_loader.cpp reads the Info table, then the loader treats the definitions
// as optional because older tags or partial logs may omit tables.
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
