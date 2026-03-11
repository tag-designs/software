

#include "txtdownload.h"
#include <taglogs.h>

TxtDownload::TxtDownload(Tag &t, QString &p, QObject *parent) : AbstractDownload(t, parent)
{
    qDebug() << "opening " << p;
    fs.open(p.toStdString(), std::fstream::out);
    if (!fs.is_open()){
        qDebug() << "file not open";
    }
}

TxtDownload::~TxtDownload(){
    if (fs.is_open())
        fs.close();
}

bool TxtDownload::dumpHeader(void) {
    qDebug() << "writing header";
    return dumpTagLogHeader(fs, tag, tag_log_output_txt);
}

int TxtDownload::dumpLog(Ack &ack) {
    qDebug() << "writing log file";
    return dumpTagLog(fs, ack, config, tag_log_output_txt);
}