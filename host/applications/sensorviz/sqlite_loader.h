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
    // Load a tag SQLite log into display streams. New SQLite logs are expected
    // to contain tagcore's mandatory streams metadata table; malformed or
    // missing metadata is an error because the database is the schema authority.
    static bool load(const QString &path, SensorLog &log, QString &error);
};

#endif
