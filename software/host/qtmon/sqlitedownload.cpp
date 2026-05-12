#include "sqlitedownload.h"

#include <QDebug>

#include <sqlitelog.h>

SqliteDownload::SqliteDownload(Tag &t, QString &p, QObject *parent) : AbstractDownload(t, parent)
{
    qDebug() << "opening " << p;
    writer = std::make_unique<SqliteTagLogWriter>(p.toStdString());
    if (writer->isOpen()) {
        qDebug() << "Database connected successfully";
    } else {
        qDebug() << "Couldn't open database file " << p << " error: "
                 << QString::fromStdString(writer->lastError());
    }
}

SqliteDownload::~SqliteDownload() = default;

bool SqliteDownload::dumpHeader(void)
{
    qDebug() << "writing header";
    if (!writer || !writer->isOpen()) {
        qDebug() << "Null db pointer";
        return false;
    }

    if (!writer->dumpHeader(tag)) {
        qDebug() << "Header write failed: " << QString::fromStdString(writer->lastError());
        return false;
    }

    return true;
}

int SqliteDownload::dumpLog(Ack &ack)
{
    qDebug() << "writing log file";
    if (!writer || !writer->isOpen()) {
        return -2;
    }

    const int len = writer->dumpLog(ack, config);
    if (len < 0) {
        qDebug() << "Log write failed: " << QString::fromStdString(writer->lastError());
    }
    return len;
}
