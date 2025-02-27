#ifndef DOWNLOAD_H
#define DOWNLOAD_H

#include <QObject>
#include <QTextStream>
#include <QMessageBox>
#include <QProgressDialog>
#include <QElapsedTimer>
#include <QTimer>
#include <fstream>

#include "tag.pb.h"
#include "tagclass.h"

class Download : public QObject
{
    Q_OBJECT
public:
    explicit Download(QObject *parent = 0): QObject(parent) {};
    void start(Tag *, std::fstream *);
    int  total(void) { return cnt;};

signals:
    void progressRangeChanged(int,int);
    void progressValueChanged(int);
    void downloadFinished();

public slots:
    void cancel();

private slots:
    void worker();

private:
    void downloadError(const QString &);
    int max_cnt;
    int cnt;
    Config config;
    std::fstream *fs;
    QMessageBox msgBox;
    Ack ack;
    QElapsedTimer timer;
    Tag *tag;
    QTimer trigger_timer;
    QProgressDialog *pd;
};

#endif // DOWNLOAD_H