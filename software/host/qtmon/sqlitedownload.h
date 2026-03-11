#ifndef SQLITEDOWNLOAD_H
#define SQLITEDOWNLOAD_H

#include "abstractdownload.h"
#include <QSqlDatabase>

class SqliteDownload: public AbstractDownload
{
    Q_OBJECT

    public: 
        SqliteDownload(Tag &t, QString &p, QObject *parent = 0);
        ~SqliteDownload();

    private:
        bool logTableCreated = false;
        bool dumpHeader(void) override; 
        int dumpLog(Ack &ack) override;
        int dumpTagLog(const CompassTagLog &log);
};

#endif