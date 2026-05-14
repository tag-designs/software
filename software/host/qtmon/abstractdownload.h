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

/**
 * Drives a tag data download without knowing the output file format.
 *
 * The download object owns a TagLogWriter, which handles text/SQLite-specific
 * persistence. AbstractDownload handles the shared mechanics: read status,
 * fetch log chunks by index, keep the UI responsive by processing bounded
 * batches on a timer, and report progress/errors to the caller.
 */
class AbstractDownload : public QObject
{
    Q_OBJECT

    public:

        AbstractDownload(Tag &t, std::unique_ptr<TagLogWriter> log_writer, QObject *parent = 0);
        virtual ~AbstractDownload() = default;
        // Starts the timer-driven download. The call returns after the first
        // setup step; work continues through worker() until finished/canceled.
        void exec(void);
        int total(void){return cnt;};

    signals:

        void progressRangeChanged(int,int);
        void progressValueChanged(int);
        void downloadFinished();

    public slots:

        // Stops the timer. This is used both for user cancellation and normal
        // completion so elapsed time/count logging stays in one place.
        void cancel();

    private slots:

        void worker();

    protected:
        int max_cnt;
        // Number of log records consumed so far. This is passed back to the
        // tag as the next download offset.
        int cnt;
        Config config;
        Tag &tag;
        Ack ack;

    private:

        void downloadError(const QString &);
        bool dumpHeader(void);
        // Returns the number of records consumed, 0 at end/no matching payload,
        // and negative values for errors. See TagLogWriter::writeLog().
        int dumpLog(Ack &ack);
        
        
        QMessageBox msgBox;
        QElapsedTimer timer;
        QTimer trigger_timer;
        QProgressDialog *pd;
        std::unique_ptr<TagLogWriter> writer;
       
};

#endif
