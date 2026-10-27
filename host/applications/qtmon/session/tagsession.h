/**
 * @file    tagsession.h
 * @brief   The one interface between qtmonitor's window and a tag: commands
 *          in, reports out.
 *
 * @details The window never touches a Tag. It sends commands to a
 *          TagSession and draws what the session reports, through
 *          MonitorModel (core/monitormodel.h). Two sessions implement it:
 *          LiveTagSession, whose worker thread owns the real Tag, and
 *          FixtureSession, which replays a captured fixture for screenshots.
 *          Every command returns at once; its outcome arrives as a signal on
 *          the GUI thread. See host/applications/qtmon/design/architecture.md.
 */

#ifndef QTMON_SESSION_TAGSESSION_H
#define QTMON_SESSION_TAGSESSION_H

#include <vector>

#include <QMetaType>
#include <QObject>
#include <QString>

#include "commands.h"
#include "linkadapt.h"
#include "tag.pb.h"
#include "taglogwriter.h"

/**
 * @class   TagSession
 * @brief   A tag, as the window sees it: commands that return at once, and
 *          signals that report what happened.
 *
 * @details Use from the GUI thread; the signals are delivered there.
 *          Commands sent while no tag is attached are ignored, except
 *          findBases() and attach(). While attached, the session reports a
 *          status about every qtmon::kStatusPollMs, and after any command
 *          that changes the tag's state.
 */
class TagSession : public QObject
{
  Q_OBJECT

public:
  using QObject::QObject;
  ~TagSession() override = default;

  /// Lists the connected tag bases; reported by basesFound().
  virtual void findBases() = 0;
  /**
   * @brief Attaches to the tag on @p base, which resets it; reported by
   *        attached() and then statuses, or attachFailed().
   * @warning Attaching ends a running deployment.
   */
  virtual void attach(const UsbDev &base) = 0;
  /// Detaches; reported by detached().
  virtual void detach() = 0;
  /// Reads the tag's configuration; reported by configRead() or commandFailed(Read).
  virtual void readConfig() = 0;
  /// Writes @p config and starts the tag; reported by startSent().
  virtual void start(const Config &config) = 0;
  /// Stops the tag; a refusal is reported by commandFailed(Stop).
  virtual void stop() = 0;
  /// Sets the tag's clock from the host's; a refusal is reported by commandFailed(Sync).
  virtual void syncClock() = 0;
  /// Runs every self-test; reported by testSent() or commandFailed(Test).
  virtual void selfTest() = 0;
  /// Erases the tag's data; reported by eraseSent() or commandFailed(Erase).
  virtual void erase() = 0;
  /**
   * @brief Downloads the log to @p path; reported by downloadProgress() and
   *        downloadFinished(). Statuses pause until it ends.
   */
  virtual void download(const QString &path, TagLogStorageFormat format) = 0;
  /**
   * @brief Asks a running download to stop after the current page, keeping
   *        what it wrote. Safe from any thread.
   */
  virtual void cancelDownload() = 0;

signals:
  /// The connected tag bases, possibly none.
  void basesFound(std::vector<UsbDev> bases);
  /// Attached: the tag's identity and configuration.
  void attached(TagInfo info, Config config);
  /// Attaching failed; @p message says so.
  void attachFailed(QString message);
  /// Detached.
  void detached();
  /**
   * @brief A status read.
   * @param[in] status   The status.
   * @param[in] voltage  Base-reported supply voltage, volts.
   * @param[in] host_ms  Host clock when it was read, ms since the epoch.
   */
  void statusRead(Status status, float voltage, qint64 host_ms);
  /// The tag's configuration, for Read Configuration.
  void configRead(Config config);
  /**
   * @brief Start was sent. @p call_message is the tag's message if
   *        Tag::Start() reported failure, empty otherwise; whether the tag
   *        started is judged from the statuses that follow (MonitorModel).
   */
  void startSent(QString call_message);
  /// Self-Test was accepted.
  void testSent();
  /// Erase was accepted; progress follows in the statuses.
  void eraseSent();
  /// The tag refused @p command, or it could not be sent; @p message says why.
  void commandFailed(qtmon::Command command, QString message);
  /// Download progress: records written, and the record count.
  void downloadProgress(int value, int maximum);
  /// The download ended; @p error is why it failed, empty when it did not.
  void downloadFinished(bool ok, QString error);
};

Q_DECLARE_METATYPE(TagInfo)
Q_DECLARE_METATYPE(Config)
Q_DECLARE_METATYPE(Status)
Q_DECLARE_METATYPE(UsbDev)
Q_DECLARE_METATYPE(std::vector<UsbDev>)
Q_DECLARE_METATYPE(qtmon::Command)
Q_DECLARE_METATYPE(TagLogStorageFormat)

#endif // QTMON_SESSION_TAGSESSION_H
