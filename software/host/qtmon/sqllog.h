
#ifndef SQL_TAG_LOGS_H
#define SQL_TAG_LOGS_H

#include <QSqlDatabase>

#include "tag.pb.h"


bool dumpTagSQLHeader(QSqlDatabase *db, Tag &t);

int dumpTagSQL(QSqlDatabase *db, 
                const Ack &log,
                const Config &config);

#endif