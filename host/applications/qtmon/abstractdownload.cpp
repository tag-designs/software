#include <abstractdownload.h>
#include <QDebug>
#include <QElapsedTimer>
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
    finished = false;
    log_started = false;
    cancel_requested.store(false);

    // Prefer the external page cursor for sparse-checkpoint IMUTagNand logs.
    // Older firmware reports only internal_data_count, so keep that fallback.
    Status status;
    if (!tag.GetStatus(status))
    {
        emit downloadFinished();
        return;
    }

    cnt = 0;
    max_cnt = status.external_data_count() > 0
        ? status.external_data_count()
        : status.internal_data_count();
    if (!max_cnt) {
        emit downloadFinished();
        return;
    }

    if (!tag.GetConfig(config)) {
        reportError(QStringLiteral("Could not read tag config"));
        emit downloadFinished();
        return;
    }

    writer = createTagLogWriter(storage_format, output_path, config);

    if (!writer || !writer->isOpen()) {
        const QString error = writer
            ? QString::fromStdString(writer->lastError())
            : QStringLiteral("No log writer configured");
        reportError(error.isEmpty() ? QStringLiteral("Download output is not open") : error);
        emit downloadFinished();
        return;
    }

    if (!writeHeader()) {
        const QString error = writer && !writer->lastError().empty()
            ? QString::fromStdString(writer->lastError())
            : QStringLiteral("Download header write failed");
        reportError(error);
        emit downloadFinished();
        return;
    }

    if (!writer->beginLog()) {
        const QString error = writer && !writer->lastError().empty()
            ? QString::fromStdString(writer->lastError())
            : QStringLiteral("Could not begin log download");
        reportError(error);
        emit downloadFinished();
        return;
    }
    log_started = true;

    emit progressRangeChanged(0,max_cnt);//max_cnt);
    emit progressValueChanged(0);
    timer.start();

    runDownloadLoop();
}

void AbstractDownload::cancel(){
    cancel_requested.store(true);
}

void AbstractDownload::runDownloadLoop(){
    int len;

    // Run flat-out in the worker thread. Each successful writer call returns
    // the number of records consumed from the current Ack, which becomes the
    // offset for the next tag.GetDataLog().
    while (!cancel_requested.load())
    {
        ack.Clear();
        len = 0;

        if (tag.GetDataLog(ack, cnt))
        {
            if (ack.error_message() != "") {
                reportError(QString::fromStdString(ack.error_message()));
                finishDownload();
                return;
            }

            if (ack.err() == Ack::NODATA && cnt < max_cnt) {
                qInfo("skipping missing log block %d", cnt);
                cnt++;
                len = 1;
                emit progressValueChanged(cnt);
                if (cnt >= max_cnt) {
                    finishDownload();
                    return;
                }
                continue;
            }

            // The writer hides text-vs-SQLite details but preserves the common
            // return convention used by the download loop.
            len = writeLog(ack);
            qDebug() << "retreived log block " << cnt << " len=" << len;
            if (len == 0) {
                qInfo("no data");
                finishDownload();
                return;
            } else if (len == -1) {
                QString error = writer && !writer->lastError().empty()
                    ? QString::fromStdString(writer->lastError())
                    : QStringLiteral("no matching log type");
                reportError(error);
                finishDownload();
                return;
            } else if (len == -2) {
                QString error = writer && !writer->lastError().empty()
                    ? QString::fromStdString(writer->lastError())
                    : QStringLiteral("no log message");
                reportError(error);
                finishDownload();
                return;   
            } else {
                cnt += len;
                emit progressValueChanged(cnt);
                if (cnt >= max_cnt) {
                    finishDownload();
                    return;
                }
         
            }
        } else {
            reportError("Parsing log failed. Unsopported tag type?");
            finishDownload();
            return;
        }

        qInfo("downloaded %d blocks",cnt);
    }

    finishDownload();
}

void AbstractDownload::reportError(const QString &s) {
    qCritical().noquote() << s;
    emit downloadError(s);
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

void AbstractDownload::finishDownload()
{
    if (finished) {
        return;
    }

    if (log_started && writer && !writer->endLog()) {
        const QString error = !writer->lastError().empty()
            ? QString::fromStdString(writer->lastError())
            : QStringLiteral("Could not finish log download");
        reportError(error);
    }
    log_started = false;

    QString tmstr = QString::number(timer.elapsed()/1000.0, 'f',2);
    qInfo() << "Download Elapsed time: " << tmstr << " seconds";
    qInfo() << "Downloaded " << cnt << " blocks";
    finished = true;
    emit progressValueChanged(cnt);
    emit downloadFinished();
}
