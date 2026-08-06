#ifndef ABSTRACTDOWNLOAD_H
#define ABSTRACTDOWNLOAD_H

#include <QObject>
#include <QTextStream>
#include <QElapsedTimer>
#include <atomic>
#include <memory>
#include <string>

#include "tag.pb.h"
#include "tagclass.h"
#include "taglogwriter.h"

/**
 * Drives a tag data download without knowing the output file format.
 *
 * AbstractDownload reads the tag config, creates the selected TagLogWriter,
 * then handles the shared mechanics: read status, fetch log chunks by index,
 * and report progress/errors to the caller. Instances are intended to run in a
 * worker thread; UI objects must stay with the MainWindow.
 */
class AbstractDownload : public QObject
{
    Q_OBJECT

    public:

        AbstractDownload(
            Tag &t,
            TagLogStorageFormat storage_format,
            std::string output_path,
            QObject *parent = 0);
        virtual ~AbstractDownload() = default;
        // Starts the blocking download loop. Call this from the worker thread.
        void exec(void);
        int total(void){return cnt;};

    signals:

        void progressRangeChanged(int,int);
        void progressValueChanged(int);
        void downloadError(const QString &);
        void downloadFinished();

    public slots:

        // Requests cancellation from any thread. The worker observes the flag
        // between tag RPCs and writer calls.
        void cancel();

    protected:
        int max_cnt;
        bool finished = false;
        bool log_started = false;
        // Number of log records consumed so far. This is passed back to the
        // tag as the next download offset.
        int cnt;
        Config config;
        Tag &tag;
        Ack ack;

    private:

        void runDownloadLoop(void);
        void finishDownload(void);
        bool writeHeader(void);
        // Returns the number of records consumed, 0 at end/no matching payload,
        // and negative values for errors. See TagLogWriter::writeLog().
        int writeLog(Ack &ack);
        
        
        std::atomic_bool cancel_requested{false};
        QElapsedTimer timer;
        TagLogStorageFormat storage_format;
        std::string output_path;
        std::unique_ptr<TagLogWriter> writer;
        void reportError(const QString &);
       
};

#endif
