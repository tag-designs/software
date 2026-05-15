#ifndef SENSORVIZ_SQLITE_LOADER_H
#define SENSORVIZ_SQLITE_LOADER_H

#include <QString>

#include "sensorstream.h"

// SqliteLoader is the only public entry point for turning an on-disk tag log
// into sensorViz's normalized SensorLog. MainWindow calls this from
// dataloading.cpp; the rest of the app should not issue SQLite queries.
class SqliteLoader
{
public:
    // Load a tag SQLite log into display streams. The loader treats sensor
    // tables as optional: a PresTag may have Pressure without Activity, while a
    // BitTag may have Activity without Pressure. Missing tables are ignored,
    // malformed present tables are errors, and at least one supported stream is
    // required for success.
    static bool load(const QString &path, SensorLog &log, QString &error);
};

#endif
