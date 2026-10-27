/**
 * @file    qtmon_session_check.cpp
 * @brief   Hardware check of qtmonitor's live tag session: one whole tag
 *          cycle through LiveTagSession and MonitorModel, with no window.
 *
 * @details Drives the session exactly as the window does: commands to a
 *          LiveTagSession, whose worker thread owns the tag, and every status
 *          through a qtmon::MonitorModel, whose events decide each step. The
 *          cycle is: attach (the only base), wait for IDLE, synchronize the
 *          clock, self-test, read the configuration and start with it
 *          unchanged, run for `--run-seconds`, stop, download to `--output`,
 *          erase, detach. Each step prints its outcome and time; the program
 *          exits 0 when every step passed, 1 at the first failure (leaving
 *          the tag attached as it is, for a capture).
 *
 * @warning Changes the tag: it is reset by the attach, started, stopped and
 *          erased. Run it only on a bench tag in IDLE with nothing to keep,
 *          following docs/bench/ (supply voltage, UUID, no other program
 *          attached). Built with -DBUILD_QTMON_CHECKS=ON; see test/README.md.
 */

#include <cmath>
#include <cstdio>
#include <functional>

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDateTime>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QTimer>

#include "livetagsession.h"
#include "monitormodel.h"
#include "startchecks.h"
#include "tagrules.h"

extern "C" {
#include "log.h"
}

namespace {

/**
 * @class   Cycle
 * @brief   The tag cycle, one step at a time, each waiting on the session's
 *          reports.
 *
 * @details Each step sends one command and installs a predicate over the
 *          model (or a report) that ends it; a step that does not end within
 *          its timeout fails the run.
 */
class Cycle : public QObject
{
public:
  /**
   * @param[in] run_seconds  How long the tag runs between start and stop.
   * @param[in] output       Download file.
   */
  Cycle(int run_seconds, QString output) : run_seconds_(run_seconds), output_(std::move(output))
  {
    connect(&session_, &TagSession::basesFound, this, [this](std::vector<UsbDev> bases) {
      if (bases.size() != 1)
        return fail(QString("expected one tag base, found %1").arg(bases.size()));
      session_.attach(bases[0]);
    });
    connect(&session_, &TagSession::attached, this, [this](TagInfo info, Config config) {
      model_.attached(info);
      config_ = config;
      std::printf("  attached %s %s, firmware %s\n", TagType_Name(info.tag_type()).c_str(),
                  info.uuid().c_str(), info.githash().c_str());
    });
    connect(&session_, &TagSession::attachFailed, this,
            [this](QString message) { fail("attach failed: " + message); });
    connect(&session_, &TagSession::statusRead, this,
            [this](Status status, float voltage, qint64 host_ms) {
              ++statuses_;
              for (const qtmon::MonitorEvent &e : model_.applyStatus(status, voltage, host_ms))
                events_.push_back(e);
              advance();
            });
    connect(&session_, &TagSession::configRead, this, [this](Config config) {
      config_ = config;
      config_read_ = true;
      advance();
    });
    connect(&session_, &TagSession::startSent, this, [this](QString message) {
      model_.startSent(QDateTime::currentMSecsSinceEpoch(), message.toStdString());
    });
    connect(&session_, &TagSession::testSent, this, [] {});
    connect(&session_, &TagSession::eraseSent, this, [this] { model_.eraseSent(); });
    connect(&session_, &TagSession::commandFailed, this,
            [this](qtmon::Command command, QString message) {
              fail(QString("%1 refused: %2").arg(qtmon::commandName(command), message));
            });
    connect(&session_, &TagSession::downloadProgress, this,
            [this](int value, int maximum) { download_value_ = value, download_max_ = maximum; });
    connect(&session_, &TagSession::downloadFinished, this, [this](bool ok, QString error) {
      model_.downloadEnded();
      download_done_ = true;
      download_ok_ = ok;
      download_error_ = error;
      advance();
    });
    connect(&session_, &TagSession::detached, this, [this] {
      model_.detached();
      detached_ = true;
      advance();
    });
    connect(&timeout_, &QTimer::timeout, this, [this] { fail("timed out"); });
    timeout_.setSingleShot(true);
  }

  /// Starts the cycle; the event loop runs it.
  void begin()
  {
    step("attach", 20, [this] { session_.findBases(); },
         [this] { return model_.attached() && model_.hasStatus() && statuses_ >= 3; });
  }

  /// 0 when every step passed.
  int result() const { return failed_ ? 1 : 0; }

private:
  /// A step: what to send, and when it is done.
  struct Step {
    const char *name;
    std::function<bool()> done;
  };

  /**
   * @brief Starts a step: prints it, arms its timeout, sends its command.
   * @param[in] name       Shown in the output.
   * @param[in] seconds    Timeout.
   * @param[in] send       Sends the command.
   * @param[in] done       True once the step has finished.
   */
  void step(const char *name, int seconds, const std::function<void()> &send,
            std::function<bool()> done)
  {
    current_ = {name, std::move(done)};
    events_.clear();
    std::printf("%-10s ", name);
    std::fflush(stdout);
    clock_.start();
    timeout_.start(seconds * 1000);
    send();
  }

  /// Ends the current step if it is done, and starts the next.
  void advance()
  {
    if (failed_ || !current_.done || !current_.done())
      return;
    timeout_.stop();
    const QString name = current_.name;
    current_ = {};
    std::printf("ok  %.1f s\n", clock_.elapsed() / 1000.0);
    next(name);
  }

  /// The step after @p previous.
  void next(const QString &previous)
  {
    if (previous == "attach") {
      if (model_.state() != IDLE)
        return fail(QString("the tag is %1, not IDLE")
                        .arg(QString::fromStdString(TagState_Name(model_.state()))));
      step("sync", 10, [this] { session_.syncClock(); }, [this] {
        return std::fabs(model_.clockOffsetS()) <= qtmon::kClockToleranceS;
      });
    } else if (previous == "sync") {
      std::printf("  clock offset %+.3f s, voltage %.2f V\n", model_.clockOffsetS(),
                  model_.voltage());
      step("self-test", 70, [this] {
             session_.selfTest();
             model_.testSent(QDateTime::currentMSecsSinceEpoch());
           },
           [this] { return finished(qtmon::MonitorEvent::Kind::TestFinished); });
    } else if (previous == "self-test") {
      if (model_.lastTest() != ALL_PASSED)
        return fail(QString("self-test is %1")
                        .arg(QString::fromStdString(TestResult_Name(model_.lastTest()))));
      std::printf("  ALL_PASSED\n");
      step("read", 10, [this] { session_.readConfig(); }, [this] { return config_read_; });
    } else if (previous == "read") {
      step("start", 20, [this] { session_.start(config_); }, [this] {
        if (finished(qtmon::MonitorEvent::Kind::StartFailed))
          fail("start failed");
        return finished(qtmon::MonitorEvent::Kind::StartSucceeded);
      });
    } else if (previous == "start") {
      std::printf("  %s; running %d s without commands\n",
                  TagState_Name(model_.state()).c_str(), run_seconds_);
      run_until_ = QDateTime::currentMSecsSinceEpoch() + run_seconds_ * 1000LL;
      step("run", run_seconds_ + 30, [] {},
           [this] { return QDateTime::currentMSecsSinceEpoch() >= run_until_; });
    } else if (previous == "run") {
      step("stop", 40, [this] { session_.stop(); },
           [this] { return model_.state() == FINISHED || model_.state() == ABORTED; });
    } else if (previous == "stop") {
      std::printf("  %s, %d internal, %d external records\n",
                  TagState_Name(model_.state()).c_str(), model_.status().internal_data_count(),
                  model_.status().external_data_count());
      step("download", 600, [this] {
             model_.downloadStarted();
             session_.download(output_, TagLogStorageFormat::Sqlite);
           },
           [this] { return download_done_; });
    } else if (previous == "download") {
      if (!download_ok_)
        return fail("download failed: " + download_error_);
      std::printf("  %d of %d records to %s (%lld bytes)\n", download_value_, download_max_,
                  qPrintable(output_), static_cast<long long>(QFileInfo(output_).size()));
      step("erase", 300, [this] { session_.erase(); },
           [this] { return finished(qtmon::MonitorEvent::Kind::EraseFinished); });
    } else if (previous == "erase") {
      step("detach", 10, [this] { session_.detach(); }, [this] { return detached_; });
    } else if (previous == "detach") {
      std::printf("session check: every step passed (%d statuses)\n", statuses_);
      QCoreApplication::exit(0);
    }
  }

  /// True when the current step has seen an event of @p kind.
  bool finished(qtmon::MonitorEvent::Kind kind) const
  {
    for (const qtmon::MonitorEvent &e : events_)
      if (e.kind == kind)
        return true;
    return false;
  }

  /// Fails the run, leaving the tag as it is.
  void fail(const QString &why)
  {
    if (failed_)
      return;
    failed_ = true;
    timeout_.stop();
    std::printf("FAILED: %s (state %s)\n", qPrintable(why),
                TagState_Name(model_.state()).c_str());
    QCoreApplication::exit(1);
  }

  LiveTagSession session_;          ///< The session under test.
  qtmon::MonitorModel model_;       ///< Interprets its reports, as the window's does.
  std::vector<qtmon::MonitorEvent> events_;  ///< The current step's model events.
  Step current_;                    ///< The step running.
  QTimer timeout_;                  ///< Fails a step that does not end.
  QElapsedTimer clock_;             ///< Times the current step.
  int run_seconds_;                 ///< See the constructor.
  QString output_;                  ///< See the constructor.
  qint64 run_until_ = 0;            ///< End of the run step, host ms.
  int statuses_ = 0;                ///< Statuses received.
  Config config_;                   ///< The tag's configuration.
  bool config_read_ = false;        ///< Read Configuration answered.
  bool download_done_ = false;      ///< The download reported its end.
  bool download_ok_ = false;        ///< ... successfully.
  QString download_error_;          ///< ... or why not.
  int download_value_ = 0;          ///< Records written.
  int download_max_ = 0;            ///< Record count.
  bool detached_ = false;           ///< Detach answered.
  bool failed_ = false;             ///< A step failed; the run is over.
};

} // namespace

/// Parses the options and runs the cycle.
int main(int argc, char *argv[])
{
  QCoreApplication app(argc, argv);
  QCommandLineParser parser;
  parser.setApplicationDescription(
      "Runs one tag cycle through qtmonitor's live session. Changes the tag.");
  parser.addHelpOption();
  parser.addOption({"run-seconds", "Seconds the tag runs between start and stop.", "s", "60"});
  parser.addOption({"output", "Download file.", "path", "qtmon-session-check.db3"});
  parser.process(app);

  // tagcore's own messages: errors only, as qtmonitor shows them by default.
  log_set_level(LOG_ERROR);
  Cycle cycle(parser.value("run-seconds").toInt(), parser.value("output"));
  QTimer::singleShot(0, &cycle, &Cycle::begin);
  app.exec();
  return cycle.result();
}
