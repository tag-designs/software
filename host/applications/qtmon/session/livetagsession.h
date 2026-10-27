/**
 * @file    livetagsession.h
 * @brief   The tag session for a real tag: a worker thread that owns the Tag
 *          and makes every monitor call.
 *
 * @details TagWorker lives on its own thread and is the only code that
 *          touches the Tag: it polls the status, runs each command, and runs
 *          downloads (DownloadJob) where they block nothing but itself.
 *          LiveTagSession is the GUI thread's handle on it: each command is
 *          queued to the worker, and the worker's signals reach the window
 *          on the GUI thread. Because one thread owns the tag, commands
 *          never overlap, and the window never waits on the tag.
 */

#ifndef QTMON_SESSION_LIVETAGSESSION_H
#define QTMON_SESSION_LIVETAGSESSION_H

#include <atomic>

#include <QThread>

#include "tagclass.h"
#include "tagsession.h"

class QTimer;

/**
 * @class   TagWorker
 * @brief   Owns the Tag on the session's worker thread.
 *
 * @details Every slot runs on the worker thread, queued by LiveTagSession,
 *          one at a time. While attached it reads the status every
 *          qtmon::kStatusPollMs, and once more after any command that changes
 *          the tag. After Synchronize Clock and Self-Test it pauses the polls
 *          briefly, while the tag cannot answer debug accesses reliably. A download runs inside download() and blocks the polls
 *          until it ends; cancelDownload() is the only member another thread
 *          may call.
 */
class TagWorker : public QObject
{
  Q_OBJECT

public:
  using QObject::QObject;

  /// Stops a running download after its current page. Any thread.
  void cancelDownload() { cancel_.store(true); }

public slots:
  void findBases();                 ///< See TagSession::findBases().
  void attach(UsbDev base);         ///< See TagSession::attach().
  void detach();                    ///< See TagSession::detach().
  void readConfig();                ///< See TagSession::readConfig().
  void start(Config config);        ///< See TagSession::start().
  void stop();                      ///< See TagSession::stop().
  void syncClock();                 ///< See TagSession::syncClock().
  void selfTest();                  ///< See TagSession::selfTest().
  void erase();                     ///< See TagSession::erase().
  /// See TagSession::download().
  void download(QString path, TagLogStorageFormat format);
  /// Reads and reports the status, when attached.
  void poll();
  /// Detaches if attached and stops polling; called before the thread ends.
  void shutdown();

signals:
  /// @name The TagSession signals, forwarded by LiveTagSession.
  ///@{
  void basesFound(std::vector<UsbDev> bases);
  void attached(TagInfo info, Config config);
  void attachFailed(QString message);
  void detached();
  void statusRead(Status status, float voltage, qint64 host_ms);
  void configRead(Config config);
  void startSent(QString call_message);
  void testSent();
  void eraseSent();
  void commandFailed(qtmon::Command command, QString message);
  void downloadProgress(int value, int maximum);
  void downloadFinished(bool ok, QString error);
  ///@}

private:
  /// The poll timer, made on first use so it belongs to the worker thread.
  QTimer *timer();
  /// The tag's message for the last refused call, or @p fallback.
  QString refusal(const QString &fallback);
  /// Skips polls for @p ms, then reads the status once.
  void quietFor(int ms);

  Tag tag_;                          ///< The tag; touched only on this thread.
  QTimer *timer_ = nullptr;          ///< See timer().
  std::atomic_bool cancel_{false};   ///< See cancelDownload().
  qint64 quiet_until_ms_ = 0;        ///< Polls are skipped until then (host ms); see quietFor().
};

/**
 * @class   LiveTagSession
 * @brief   TagSession over a TagWorker on its own thread.
 *
 * @details Construction starts the thread; destruction detaches the tag and
 *          waits for the thread to end.
 */
class LiveTagSession : public TagSession
{
  Q_OBJECT

public:
  explicit LiveTagSession(QObject *parent = nullptr);
  ~LiveTagSession() override;

  void findBases() override;
  void attach(const UsbDev &base) override;
  void detach() override;
  void readConfig() override;
  void start(const Config &config) override;
  void stop() override;
  void syncClock() override;
  void selfTest() override;
  void erase() override;
  void download(const QString &path, TagLogStorageFormat format) override;
  void cancelDownload() override;

private:
  QThread thread_;               ///< The worker's thread.
  TagWorker *worker_ = nullptr;  ///< Owned; lives on thread_.
};

#endif // QTMON_SESSION_LIVETAGSESSION_H
