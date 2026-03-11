#ifndef ABSTRACTDOWNLOAD_H
#define ABSTRACTDOWNLOAD_H

#include <QObject>
#include <QTextStream>
#include <QMessageBox>
#include <QProgressDialog>
#include <QElapsedTimer>
#include <QTimer>
#include "tag.pb.h"
#include "tagclass.h"

class AbstractDownload : public QObject
{
    Q_OBJECT

    public:

        AbstractDownload(Tag &t, QObject *parent = 0) : tag(t), QObject(parent){}
        virtual ~AbstractDownload() = default;
        void exec(void);
        int total(void){return cnt;};

    signals:

        void progressRangeChanged(int,int);
        void progressValueChanged(int);
        void downloadFinished();

    public slots:

        void cancel();

    private slots:

        void worker();

    protected:
        int max_cnt;
        int cnt;
        Config config;
        Tag &tag;
        Ack ack;

    private:

        void downloadError(const QString &);
        virtual bool dumpHeader(void) = 0;
        virtual int dumpLog(Ack &ack) = 0;
        
        
        QMessageBox msgBox;
        QElapsedTimer timer;
        QTimer trigger_timer;
        QProgressDialog *pd;
       
};

#endif