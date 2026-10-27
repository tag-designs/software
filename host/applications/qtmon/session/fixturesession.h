/**
 * @file    fixturesession.h
 * @brief   The tag session for documentation screenshots: a captured tag
 *          replayed from a fixture, with no hardware.
 *
 * @details `qtmonitor --fake-fixture <file>` uses this session in place of
 *          LiveTagSession, so the window, the model and every widget behave
 *          as they do with a real tag; only the reports come from the file.
 *          Commands are logged and ignored. Fixtures live in
 *          host/docs/fixtures/qtmonitor/; see README.md "Fixture Screenshot
 *          Mode".
 */

#ifndef QTMON_SESSION_FIXTURESESSION_H
#define QTMON_SESSION_FIXTURESESSION_H

#include <QMap>

#include "tagsession.h"

/**
 * @class   FixtureSession
 * @brief   Replays one fixture: its TagInfo, Config, voltage and named
 *          statuses.
 *
 * @details Its reports are emitted at once, on the calling thread, so a
 *          screenshot can be taken straight after showState().
 */
class FixtureSession : public TagSession
{
  Q_OBJECT

public:
  using TagSession::TagSession;

  /**
   * @brief Loads a fixture.
   *
   * @details The fixture supplies TagInfo, Config (inline as `value`, or as a
   *          `$ref` path), a voltage and one or more named Status messages,
   *          all as protobuf JSON. A path that does not exist as given is
   *          tried relative to the source tree.
   *
   * @param[in] path  Fixture JSON.
   * @return true when every required part parsed; false, with a warning
   *         logged, otherwise.
   */
  bool load(const QString &path);

  /// The fixture's id, for screenshot file names; "fixture" if it has none.
  QString id() const { return id_; }

  /**
   * @brief Reports the fixture's status called @p name as a status read.
   *
   * @details The status of that name when the fixture has one, else its
   *          `idle`, `current` or first status. The state is forced to the
   *          name (idle, running, finished), the clock to now, an unset
   *          self-test to ALL_PASSED, and empty log counts to placeholders
   *          for running and finished, so one idle capture can show all
   *          three screens.
   */
  void showState(const QString &name);

  /// Reports one base, so the window can attach without hardware.
  void findBases() override;
  /// Reports the fixture's TagInfo and Config as attached.
  void attach(const UsbDev &base) override;
  /// Logged and ignored: a fixture stays attached.
  void detach() override;
  void readConfig() override;      ///< Logged and ignored.
  void start(const Config &config) override;  ///< Logged and ignored.
  void stop() override;            ///< Logged and ignored.
  void syncClock() override;       ///< Logged and ignored.
  void selfTest() override;        ///< Logged and ignored.
  void erase() override;           ///< Logged and ignored.
  /// Logged and reported as finished, writing nothing.
  void download(const QString &path, TagLogStorageFormat format) override;
  void cancelDownload() override {}  ///< Nothing to cancel.

private:
  /// Logs that @p command is ignored in fixture mode.
  void ignore(const char *command) const;

  QString id_ = QStringLiteral("fixture");  ///< See id().
  TagInfo info_;                  ///< The fixture's identity.
  Config config_;                 ///< The fixture's configuration.
  float voltage_ = 0.0f;          ///< Reported with every status, volts.
  QMap<QString, Status> statuses_;  ///< By lower-case name ("idle", "running", …).
};

#endif // QTMON_SESSION_FIXTURESESSION_H
