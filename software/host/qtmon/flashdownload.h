#ifndef FLASHDOWNLOAD_H
#define FLASHDOWNLOAD_H

#include <QObject>
#include <QTextStream>
#include "tag.pb.h"
//#include "host.pb.h"
#include "tagclass.h"

class FlashDownload : public QObject
{
    Q_OBJECT
public:
    explicit FlashDownload(QObject *parent = 0);

private slots:
    void download();

signals:
    void finished();
    void progressRangeChanged(int,int);
    void progressValueChanged(int);

public slots:
    void start(Tag *t, QTextStream *_stream, unsigned int _firstpage,
               unsigned int _pages, bool _writeCSV);

private:
    int firstpage;
    int pages;
    int cnt;
    QTextStream *stream;
    bool writeCSV;
    Tag *tag;
};

#endif // FLASHDOWNLOAD_H
