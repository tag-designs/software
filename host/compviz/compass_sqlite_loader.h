#ifndef COMPASS_SQLITE_LOADER_H
#define COMPASS_SQLITE_LOADER_H

#include "compass_types.h"

class SqliteDatabase;
class QString;

bool loadCompassData(
    SqliteDatabase &db,
    CompassLogData &log,
    QString &warning,
    QString &error);

#endif // COMPASS_SQLITE_LOADER_H
