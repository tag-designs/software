#include <QObject>
#include <QTime>
#include <QTimer>
#include <QDebug>
#include <QElapsedTimer>
#include <QDeadlineTimer>
#include <QFileDialog>
#include <QTimer>

#include "download.h"
#include <taglogs.h>

extern "C"
{
#include "log.h"
}

using namespace std::chrono_literals;

void Download::start(Tag *t, std::fstream *file_stream)
{
    tag = t;
    fs = file_stream;
    if (!tag) {
        downloadError(QString("Tag undefined"));
        emit downloadFinished();
        return;
    }
    // Get tag configuration and status
    
    Status status;
    if (!tag->GetStatus(status))
    {
        emit downloadFinished();
        return;
    }

    cnt = 0;
    max_cnt = status.internal_data_count();
    if (!max_cnt) {
        emit downloadFinished();
        return;
    }

    tag->GetConfig(config);

    // dump the file header

    dumpTagLogHeader(*fs, *tag, tag_log_output_txt);

    // create the progress dialog

    emit progressRangeChanged(0,15000);//max_cnt);
    emit progressValueChanged(0);

    // trigger worker

    connect(&trigger_timer, &QTimer::timeout, this, &Download::worker);
    trigger_timer.start();
    timer.start();
    
    return;
}

void Download::cancel(){
    trigger_timer.stop();
    QString tmstr = QString::number(timer.elapsed()/1000.0, 'f',2);
    qInfo() << "Download Elapsed time: " << tmstr << " seconds";
    qInfo() << "Downloaded " << cnt << " blocks";
}

void Download::worker(){
    QDeadlineTimer deadline(300ms);
    int len;

    // do as much work as possible within the deadline

    do
    {
        ack.Clear();
        len = 0;

        // grab as much data as possible

        if (tag->GetDataLog(ack, cnt))
        {
            if (ack.error_message() != "") {
                downloadError(QString::fromStdString(ack.error_message()));
                emit downloadFinished();
                return;
            }
            len = dumpTagLog(*fs, ack, config, tag_log_output_txt);
            if (len == 0) {
                qInfo("no data");
                emit downloadFinished();
                cancel();
                return;
            } else if (len == -1) {
                downloadError("no matching log type");
                emit downloadFinished();
                cancel();
                return;
            } else {
                cnt += len;
         
            }
        } else {
            downloadError("Parsing log failed. Unsopported tag type?");
            emit downloadFinished();
            cancel();
            return;
        }
    } while ((len>0) && !deadline.hasExpired());

    // update progress info
    
    qInfo("downloaded %d blocks",cnt);
    emit progressValueChanged(cnt);

    if (len == 0) {
        // finished
        emit downloadFinished();
        cancel();
    } 
}

void Download::downloadError(const QString &s) {
    msgBox.setText(s);
    msgBox.exec();
    qDebug() << s;
}