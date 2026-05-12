#ifndef SQLITEDOWNLOAD_H
#define SQLITEDOWNLOAD_H

#include "abstractdownload.h"

#include <memory>

class SqliteTagLogWriter;

class SqliteDownload: public AbstractDownload
{
    Q_OBJECT

    public: 
        SqliteDownload(Tag &t, QString &p, QObject *parent = 0);
        ~SqliteDownload();

    private:
        std::unique_ptr<SqliteTagLogWriter> writer;
        bool dumpHeader(void) override; 
        int dumpLog(Ack &ack) override;
};

#endif
