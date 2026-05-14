#include <abstractdownload.h>
#include <QElapsedTimer>
#include <QTimer>
#include <QDeadlineTimer>
#include <utility>

AbstractDownload::AbstractDownload(Tag &t, std::unique_ptr<TagLogWriter> log_writer, QObject *parent)
    : QObject(parent), tag(t), writer(std::move(log_writer))
{
}

void AbstractDownload::exec() {
    // Get tag configuration and status
    
    Status status;
    if (!tag.GetStatus(status))
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

    tag.GetConfig(config);

    if (!writer || !writer->isOpen()) {
        const QString error = writer
            ? QString::fromStdString(writer->lastError())
            : QStringLiteral("No log writer configured");
        downloadError(error.isEmpty() ? QStringLiteral("Download output is not open") : error);
        emit downloadFinished();
        return;
    }

    if (!dumpHeader()) {
        downloadError("Download header write failed");
        emit downloadFinished();
        return;
    }

    // create the progress dialog

    emit progressRangeChanged(0,max_cnt);//max_cnt);
    emit progressValueChanged(0);

    // trigger worker

    connect(&trigger_timer, &QTimer::timeout, this, &AbstractDownload::worker);
    trigger_timer.start();
    timer.start();
    
    return;
}

void AbstractDownload::cancel(){
    trigger_timer.stop();
    QString tmstr = QString::number(timer.elapsed()/1000.0, 'f',2);
    qInfo() << "Download Elapsed time: " << tmstr << " seconds";
    qInfo() << "Downloaded " << cnt << " blocks";
}

void AbstractDownload::worker(){
    QDeadlineTimer deadline(300ms);
    int len;

    // do as much work as possible within the deadline

    do
    {
        ack.Clear();
        len = 0;

        // grab as much data as possible

        if (tag.GetDataLog(ack, cnt))
        {
            if (ack.error_message() != "") {
                downloadError(QString::fromStdString(ack.error_message()));
                emit downloadFinished();
                return;
            }

            len = dumpLog(ack);
            if (len == 0) {
                qInfo("no data");
                emit downloadFinished();
                cancel();
                return;
            } else if (len == -1) {
                QString error = writer && !writer->lastError().empty()
                    ? QString::fromStdString(writer->lastError())
                    : QStringLiteral("no matching log type");
                downloadError(error);
                emit downloadFinished();
                cancel();
                return;
            } else if (len == -2) {
                QString error = writer && !writer->lastError().empty()
                    ? QString::fromStdString(writer->lastError())
                    : QStringLiteral("no log message");
                downloadError(error);
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

void AbstractDownload::downloadError(const QString &s) {
    msgBox.setText(s);
    msgBox.exec();
    qDebug() << s;
}

bool AbstractDownload::dumpHeader()
{
    return writer && writer->writeHeader(tag);
}

int AbstractDownload::dumpLog(Ack &ack)
{
    if (!writer) {
        return -2;
    }
    return writer->writeLog(ack, config);
}
