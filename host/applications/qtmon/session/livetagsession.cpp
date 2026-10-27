/**
 * @file    livetagsession.cpp
 * @brief   TagWorker and LiveTagSession, declared in livetagsession.h.
 */

#include "livetagsession.h"

#include <QDateTime>
#include <QDebug>
#include <QTimer>

#include "downloadjob.h"
#include "monitormodel.h"

namespace {

/// Delay before the status read that follows Stop, ms: the state machine
/// acts on a stop after acknowledging it.
constexpr int kAfterStopMs = 250;

/// Sends one of the download's messages to Qt's log, which the Error Log tab shows.
void logDownload(qtmon::LogLevel level, const std::string &text)
{
  const QString message = QString::fromStdString(text);
  switch (level) {
  case qtmon::LogLevel::Debug:
    qDebug().noquote() << message;
    break;
  case qtmon::LogLevel::Info:
    qInfo().noquote() << message;
    break;
  case qtmon::LogLevel::Warning:
    qWarning().noquote() << message;
    break;
  case qtmon::LogLevel::Error:
    qCritical().noquote() << message;
    break;
  }
}

} // namespace

// ---- TagWorker: runs on the worker thread ----

QTimer *TagWorker::timer()
{
  if (!timer_) {
    timer_ = new QTimer(this);
    timer_->setInterval(qtmon::kStatusPollMs);
    connect(timer_, &QTimer::timeout, this, &TagWorker::poll);
  }
  return timer_;
}

QString TagWorker::refusal(const QString &fallback)
{
  const std::string message = tag_.DebugMessage();
  return message.empty() ? fallback : QString::fromStdString(message);
}

void TagWorker::findBases()
{
  std::vector<UsbDev> bases;
  if (!tag_.Available(bases))
    bases.clear();
  emit basesFound(bases);
}

void TagWorker::attach(UsbDev base)
{
  if (tag_.IsAttached()) {
    emit attachFailed(tr("Already Attached to Tag"));
    return;
  }
  if (!tag_.Attach(base)) {
    emit attachFailed(tr("Could not attach to tag"));
    return;
  }
  TagInfo info;
  Config config;
  tag_.GetTagInfo(info);
  tag_.GetConfig(config);
  emit attached(info, config);
  poll();
  timer()->start();
}

void TagWorker::detach()
{
  timer()->stop();
  if (tag_.IsAttached())
    tag_.Detach();
  emit detached();
}

void TagWorker::shutdown()
{
  if (timer_)
    timer_->stop();
  if (tag_.IsAttached())
    tag_.Detach();
}

void TagWorker::poll()
{
  if (!tag_.IsAttached())
    return;
  float voltage = 0.0f;
  tag_.Voltage(voltage);
  Status status;
  // A failed read is skipped; the next poll tries again.
  if (tag_.GetStatus(status))
    emit statusRead(status, voltage, QDateTime::currentMSecsSinceEpoch());
}

void TagWorker::readConfig()
{
  if (!tag_.IsAttached())
    return;
  Config config;
  if (tag_.GetConfig(config))
    emit configRead(config);
  else
    emit commandFailed(qtmon::Command::Read, tr("Tag config read failed"));
}

void TagWorker::start(Config config)
{
  if (!tag_.IsAttached())
    return;
  qDebug().noquote() << "Starting tag with config:"
                     << QString::fromStdString(config.DebugString());
  // A false return does not mean the tag did not start: the acknowledgement
  // can be lost after the tag accepted the command. MonitorModel judges the
  // start from the statuses that follow.
  const QString message =
      tag_.Start(config) ? QString() : QString::fromStdString(tag_.DebugMessage());
  emit startSent(message);
  poll();
}

void TagWorker::stop()
{
  if (!tag_.IsAttached())
    return;
  if (!tag_.Stop()) {
    emit commandFailed(qtmon::Command::Stop, tr("The tag did not accept the stop command."));
    return;
  }
  QTimer::singleShot(kAfterStopMs, this, &TagWorker::poll);
}

void TagWorker::syncClock()
{
  if (!tag_.IsAttached())
    return;
  if (!tag_.SetRtc()) {
    emit commandFailed(qtmon::Command::Sync, QString::fromStdString(tag_.DebugMessage()));
    return;
  }
  poll();
}

void TagWorker::selfTest()
{
  if (!tag_.IsAttached())
    return;
  if (!tag_.Test(RUN_ALL)) {
    emit commandFailed(qtmon::Command::Test,
                       refusal(tr("The tag did not accept the self-test command.")));
    return;
  }
  emit testSent();
}

void TagWorker::erase()
{
  if (!tag_.IsAttached())
    return;
  if (!tag_.Erase()) {
    emit commandFailed(qtmon::Command::Erase, tr("The tag did not accept the erase command."));
    return;
  }
  emit eraseSent();
  poll();
}

void TagWorker::download(QString path, TagLogStorageFormat format)
{
  if (!tag_.IsAttached()) {
    emit downloadFinished(false, tr("No tag is attached"));
    return;
  }
  // The download makes its own monitor calls; the polls wait for it.
  timer()->stop();
  cancel_.store(false);
  qtmon::DownloadJob job(tag_, format, path.toStdString());
  const qtmon::DownloadResult result = job.run(
      cancel_, [this](int value, int maximum) { emit downloadProgress(value, maximum); },
      logDownload);
  emit downloadFinished(result.ok, QString::fromStdString(result.error));
  poll();
  timer()->start();
}

// ---- LiveTagSession: the GUI thread's handle ----

LiveTagSession::LiveTagSession(QObject *parent) : TagSession(parent)
{
  qRegisterMetaType<TagInfo>();
  qRegisterMetaType<Config>();
  qRegisterMetaType<Status>();
  qRegisterMetaType<UsbDev>();
  qRegisterMetaType<std::vector<UsbDev>>();
  qRegisterMetaType<qtmon::Command>();
  qRegisterMetaType<TagLogStorageFormat>();

  worker_ = new TagWorker();
  worker_->moveToThread(&thread_);
  // Worker to session: across threads, so each report is queued to the GUI thread.
  connect(worker_, &TagWorker::basesFound, this, &TagSession::basesFound);
  connect(worker_, &TagWorker::attached, this, &TagSession::attached);
  connect(worker_, &TagWorker::attachFailed, this, &TagSession::attachFailed);
  connect(worker_, &TagWorker::detached, this, &TagSession::detached);
  connect(worker_, &TagWorker::statusRead, this, &TagSession::statusRead);
  connect(worker_, &TagWorker::configRead, this, &TagSession::configRead);
  connect(worker_, &TagWorker::startSent, this, &TagSession::startSent);
  connect(worker_, &TagWorker::testSent, this, &TagSession::testSent);
  connect(worker_, &TagWorker::eraseSent, this, &TagSession::eraseSent);
  connect(worker_, &TagWorker::commandFailed, this, &TagSession::commandFailed);
  connect(worker_, &TagWorker::downloadProgress, this, &TagSession::downloadProgress);
  connect(worker_, &TagWorker::downloadFinished, this, &TagSession::downloadFinished);
  thread_.setObjectName(QStringLiteral("qtmonitor tag"));
  thread_.start();
}

LiveTagSession::~LiveTagSession()
{
  // A running download stops after its page; then the tag is detached on its
  // own thread before the thread ends.
  worker_->cancelDownload();
  QMetaObject::invokeMethod(worker_, &TagWorker::shutdown, Qt::BlockingQueuedConnection);
  thread_.quit();
  thread_.wait();
  delete worker_;
}

void LiveTagSession::findBases()
{
  QMetaObject::invokeMethod(worker_, &TagWorker::findBases, Qt::QueuedConnection);
}

void LiveTagSession::attach(const UsbDev &base)
{
  QMetaObject::invokeMethod(worker_, [w = worker_, base]() { w->attach(base); },
                            Qt::QueuedConnection);
}

void LiveTagSession::detach()
{
  QMetaObject::invokeMethod(worker_, &TagWorker::detach, Qt::QueuedConnection);
}

void LiveTagSession::readConfig()
{
  QMetaObject::invokeMethod(worker_, &TagWorker::readConfig, Qt::QueuedConnection);
}

void LiveTagSession::start(const Config &config)
{
  QMetaObject::invokeMethod(worker_, [w = worker_, config]() { w->start(config); },
                            Qt::QueuedConnection);
}

void LiveTagSession::stop()
{
  QMetaObject::invokeMethod(worker_, &TagWorker::stop, Qt::QueuedConnection);
}

void LiveTagSession::syncClock()
{
  QMetaObject::invokeMethod(worker_, &TagWorker::syncClock, Qt::QueuedConnection);
}

void LiveTagSession::selfTest()
{
  QMetaObject::invokeMethod(worker_, &TagWorker::selfTest, Qt::QueuedConnection);
}

void LiveTagSession::erase()
{
  QMetaObject::invokeMethod(worker_, &TagWorker::erase, Qt::QueuedConnection);
}

void LiveTagSession::download(const QString &path, TagLogStorageFormat format)
{
  QMetaObject::invokeMethod(
      worker_, [w = worker_, path, format]() { w->download(path, format); },
      Qt::QueuedConnection);
}

void LiveTagSession::cancelDownload()
{
  worker_->cancelDownload();
}
