#ifndef ABSTRACTDOWNLOAD_H
#define ABSTRACTDOWNLOAD_H

#include <QObject>
#include <QTextStream>
#include <QMessageBox>
#include <QProgressDialog>
#include <QElapsedTimer>
#include <QTimer>
#include <memory>

#include "tag.pb.h"
#include "tagclass.h"
#include "taglogwriter.h"

class AbstractDownload : public QObject
{
    Q_OBJECT

    public:

        AbstractDownload(Tag &t, std::unique_ptr<TagLogWriter> log_writer, QObject *parent = 0);
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
        bool dumpHeader(void);
        int dumpLog(Ack &ack);
        
        
        QMessageBox msgBox;
        QElapsedTimer timer;
        QTimer trigger_timer;
        QProgressDialog *pd;
        std::unique_ptr<TagLogWriter> writer;
       
};

#endif
