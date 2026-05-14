#include <abstractdownload.h>
#include <QDebug>
#include <QElapsedTimer>
#include <QTimer>
#include <QDeadlineTimer>
#include <utility>

AbstractDownload::AbstractDownload(
    Tag &t,
    TagLogStorageFormat storage_format,
    std::string output_path,
    QObject *parent)
    : QObject(parent),
      tag(t),
      storage_format(storage_format),
      output_path(std::move(output_path))
{
}

void AbstractDownload::exec() {
    // The tag reports the total number of internal log records in status. The
    // writer also needs the config to decode each Ack into the right schema.
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

    if (!tag.GetConfig(config)) {
        downloadError(QStringLiteral("Could not read tag config"));
        emit downloadFinished();
        return;
    }

    writer = createTagLogWriter(storage_format, output_path, config);

    if (!writer || !writer->isOpen()) {
        const QString error = writer
            ? QString::fromStdString(writer->lastError())
            : QStringLiteral("No log writer configured");
        downloadError(error.isEmpty() ? QStringLiteral("Download output is not open") : error);
        emit downloadFinished();
        return;
    }

    if (!writeHeader()) {
        const QString error = writer && !writer->lastError().empty()
            ? QString::fromStdString(writer->lastError())
            : QStringLiteral("Download header write failed");
        downloadError(error);
        emit downloadFinished();
        return;
    }

    emit progressRangeChanged(0,max_cnt);//max_cnt);
    emit progressValueChanged(0);

    // Keep the modal progress dialog responsive by doing bounded work in the
    // event loop instead of downloading the whole log in this call.
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

    // Process as many records as we can within a short deadline. Each
    // successful writer call returns the number of records consumed from the
    // current Ack, which becomes the offset for the next tag.GetDataLog().
    do
    {
        ack.Clear();
        len = 0;

        if (tag.GetDataLog(ack, cnt))
        {
            if (ack.error_message() != "") {
                downloadError(QString::fromStdString(ack.error_message()));
                emit downloadFinished();
                return;
            }

            // The writer hides text-vs-SQLite details but preserves the common
            // return convention used by the download loop.
            len = writeLog(ack);
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

    qInfo("downloaded %d blocks",cnt);
    emit progressValueChanged(cnt);

    if (len == 0) {
        // finished
        emit downloadFinished();
        cancel();
    } 
}

void AbstractDownload::downloadError(const QString &s) {
    qCritical().noquote() << s;
    msgBox.setText(s);
    msgBox.exec();
}

bool AbstractDownload::writeHeader()
{
    return writer && writer->writeHeader(tag);
}

int AbstractDownload::writeLog(Ack &ack)
{
    if (!writer) {
        return -2;
    }
    return writer->writeLog(ack);
}
