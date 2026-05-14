#ifndef SENSORVIZ_SQLITE_LOADER_H
#define SENSORVIZ_SQLITE_LOADER_H

#include <QString>

#include "sensorstream.h"

class SqliteLoader
{
public:
    static bool load(const QString &path, SensorLog &log, QString &error);
};

#endif
