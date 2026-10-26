/**
 * @file    programsession.cpp
 * @brief   The program sequence's state machine.
 */

#include "programsession.h"

#include <QDebug>
#include <QFile>

#include "programmercli.h"

namespace
{

/// Ticks (seconds) a running tag may take to stop.
constexpr int kStopTicks = 30;
/// Attach attempts before asking for a power cycle.
constexpr int kConnectTicks = 6;
/// Ticks the self-tests may take.
constexpr int kTestTicks = 120;

// Fallback for older IMUTag firmware that predates
// Status.erase_sectors_total_plus_one. New firmware reports the erase total
// directly, so the session needs no tag-specific page geometry.
constexpr int kImuDataLogPageBytes = 2048;
/// External flash erase sector, bytes, for the same fallback.
constexpr int kSectorBytes = 4096;

/// Number of @p sector-byte sectors needed for @p bytes, rounded up; 0 when either is not positive.
int sectorsFor(qint64 bytes, int sector)
{
  return bytes > 0 && sector > 0 ? static_cast<int>((bytes + sector - 1) / sector) : 0;
}

/// Whether two git hashes name the same commit, either being a prefix of the other.
bool sameCommit(const QString &a, const QString &b)
{
  return !a.isEmpty() && !b.isEmpty() &&
         (a.startsWith(b, Qt::CaseInsensitive) || b.startsWith(a, Qt::CaseInsensitive));
}

/// TagState name for messages.
QString stateName(TagState state)
{
  return QString::fromStdString(TagState_Name(state));
}

} // namespace

ProgramSession::ProgramSession(Tag &tag, ProgrammerCli &cli, QObject *parent)
    : QObject(parent), tag_(tag), cli_(cli)
{
  timer_.setInterval(1000);
  connect(&timer_, &QTimer::timeout, this, &ProgramSession::tick);
}

void ProgramSession::setStep(Step step, const QString &text)
{
  step_ = step;
  qInfo().noquote() << text;
  emit stepChanged(step, text);
}

bool ProgramSession::poll(Status &status)
{
  return tag_.IsAttached() && tag_.GetStatus(status);
}

void ProgramSession::start(const FirmwareImage &image, const QString &serial,
                           std::function<bool()> attach)
{
  if (running_)
    return;
  running_ = true;
  retestOnly_ = false;
  mismatch_ = false;
  waitTicks_ = 0;
  result_ = SessionResult();
  image_ = image;
  serial_ = serial;
  attach_ = std::move(attach);

  if (!tag_.IsAttached()) {
    // A blank board, or one whose firmware does not answer: nothing to stop
    // or erase through the firmware, so program at once.
    runProgrammer();
    return;
  }
  setStep(Step::Stopping, tr("Stopping the tag"));
  timer_.start();
}

void ProgramSession::retest(const FirmwareImage &image, const QString &serial,
                            std::function<bool()> attach)
{
  if (running_)
    return;
  running_ = true;
  retestOnly_ = true;
  mismatch_ = false;
  waitTicks_ = 0;
  result_ = SessionResult();
  image_ = image;
  serial_ = serial;
  attach_ = std::move(attach);
  if (!tag_.IsAttached() && !(attach_ && attach_())) {
    finish(tr("FAILED: the tag does not answer"), tr("The tag does not answer, so it "
                                                     "cannot be tested."));
    return;
  }
  if (!image_.usable()) {
    beginTests();
    return;
  }
  Status status;
  if (!poll(status) || status.state() != IDLE) {
    // Reading resets the tag; only an IDLE one loses nothing by it.
    result_.flash = SessionResult::Flash::Unread;
    result_.flashNote = tr("the tag was not IDLE, so its flash was not read");
    beginTests();
    return;
  }
  readFlash();
}

void ProgramSession::readFlash()
{
  const ImageFlashRange range = imageFlashRange(image_.elfPath);
  if (!range.error.isEmpty()) {
    result_.flash = SessionResult::Flash::Unread;
    result_.flashNote = range.error;
    beginTests();
    return;
  }
  if (!flashDir_.isValid()) {
    result_.flash = SessionResult::Flash::Unread;
    result_.flashNote = tr("no temporary directory for the read-back");
    beginTests();
    return;
  }
  const QString file = flashDir_.filePath(QStringLiteral("readback.bin"));
  QFile::remove(file);
  tag_.Detach();  // the programmer needs the ST-LINK
  setStep(Step::Reading, tr("Reading the tag's flash to compare with %1")
                             .arg(image_.displayName()));
  cli_.readMemory(serial_, range.start, range.size, file,
                  [this, range, file](int code, const QString &output) {
                    QFile read(file);
                    if (code != 0 || !read.open(QIODevice::ReadOnly)) {
                      result_.flash = SessionResult::Flash::Unread;
                      result_.flashNote = tr("STM32_Programmer_CLI could not read it (%1)")
                                              .arg(parseConnect(output, code).error);
                    } else {
                      result_.flashNote =
                          compareImageFlash(image_.elfPath, range.start, read.readAll());
                      result_.flash = result_.flashNote.isEmpty()
                                          ? SessionResult::Flash::Matches
                                          : SessionResult::Flash::Differs;
                      if (result_.flash == SessionResult::Flash::Differs)
                        qWarning().noquote() << result_.flashNote;
                    }
                    QFile::remove(file);
                    waitTicks_ = 0;
                    setStep(Step::Connecting, tr("Connecting to the tag again"));
                    timer_.start();
                  });
}

void ProgramSession::retryConnect()
{
  if (!running_ || step_ != Step::PowerCycle)
    return;
  waitTicks_ = 0;
  setStep(Step::Connecting, tr("Connecting to the new image"));
  timer_.start();
}

bool ProgramSession::cancel()
{
  if (!running_)
    return false;
  if (step_ == Step::PowerCycle) {
    finish(QStringLiteral("NO_ANSWER"),
           tr("Programmed, but the tag never answered. Power-cycle it and press Attach; "
              "if it still does not answer, program it again."));
    return true;
  }
  if (step_ == Step::Stopping || step_ == Step::Erasing) {
    finish(QStringLiteral("CANCELLED"), tr("Cancelled before programming."));
    return true;
  }
  return false;
}

void ProgramSession::runProgrammer()
{
  timer_.stop();
  tag_.Detach();  // the programmer needs the ST-LINK
  setStep(Step::Programming, tr("Programming %1: %2").arg(image_.displayName(),
                                                          image_.plan.summary()));
  result_.programmed = true;
  cli_.program(serial_, image_.elfPath, image_.plan.eraseArgs(),
               [this](int code, const QString &output) {
                 if (code != 0) {
                   const ConnectInfo info = parseConnect(output, code);
                   finish(QStringLiteral("PROGRAM_FAILED: %1").arg(info.error),
                          tr("STM32_Programmer_CLI failed (%1). Its output is in the log.")
                              .arg(info.error));
                   return;
                 }
                 waitTicks_ = 0;
                 setStep(Step::Connecting, tr("Connecting to the new image"));
                 timer_.start();
               });
}

void ProgramSession::beginTests()
{
  TagInfo info;
  if (tag_.GetTagInfo(info)) {
    result_.uuid = QString::fromStdString(info.uuid());
    result_.gitHash = QString::fromStdString(info.githash());
  }
  if (!retestOnly_) {
    const QString expected =
        image_.identity.gitHash.isEmpty() ? image_.manifestCommit : image_.identity.gitHash;
    mismatch_ = !expected.isEmpty() && !sameCommit(expected, result_.gitHash);
    if (mismatch_)
      qWarning().noquote() << "The tag reports commit" << result_.gitHash
                           << "but the image is" << expected;
  }
  tag_.SetRtc();
  tag_.Test(RUN_ALL);
  waitTicks_ = 0;
  setStep(Step::Testing, tr("Running the self-tests"));
  timer_.start();
}

void ProgramSession::finish(const QString &result, const QString &message)
{
  timer_.stop();
  running_ = false;
  result_.result = result;
  result_.message = message;
  setStep(Step::Done, message);
  emit finished(result_);
}

void ProgramSession::tick()
{
  Status status;
  const bool answered = poll(status);
  const TagState state = answered ? status.state() : STATE_UNSPECIFIED;

  switch (step_) {
  case Step::Stopping:
    if (!answered) {
      finish(QStringLiteral("FAILED: the tag stopped answering"),
             tr("The tag stopped answering before it was programmed."));
      return;
    }
    if (state == ABORTED || state == FINISHED) {
      // The firmware that wrote the log erases it: it knows the layout.
      if (!tag_.Erase()) {
        finish(QStringLiteral("FAILED: the tag refused the erase"),
               tr("The tag refused to erase its log."));
        return;
      }
      waitTicks_ = 0;
      setStep(Step::Erasing, tr("The tag is erasing its log"));
      return;
    }
    if (state != IDLE) {
      // Stop a running tag once; TEST and an erase in progress end by themselves.
      if (waitTicks_ == 0 && state != TEST && state != sRESET)
        tag_.Stop();
      if (++waitTicks_ > kStopTicks)
        finish(QStringLiteral("FAILED: the tag stayed %1").arg(stateName(state)),
               tr("The tag stayed %1. Stop and erase it in Tag Monitor, then program "
                  "again.").arg(stateName(state)));
      return;
    }
    runProgrammer();
    return;

  case Step::Erasing:
    if (answered && state == sRESET) {
      int total = status.erase_sectors_total_plus_one() > 0
                      ? status.erase_sectors_total_plus_one() - 1
                      : 0;
      if (total == 0) {
        TagInfo info;
        if (tag_.GetTagInfo(info))
          total = info.tag_type() == IMUTAG
                      ? sectorsFor(qint64(status.external_data_count()) * kImuDataLogPageBytes,
                                   kSectorBytes)
                      : sectorsFor(info.extflashsz(), kSectorBytes);
      }
      emit eraseProgress(status.sectors_erased(), total);
      return;
    }
    if (answered && (state == ABORTED || state == FINISHED))
      return;  // the erase has not begun its sweep yet
    if (!answered || state != IDLE) {
      finish(QStringLiteral("FAILED: no IDLE after the erase"),
             tr("The tag did not return to IDLE after its erase (it is %1).")
                 .arg(answered ? stateName(state) : tr("not answering")));
      return;
    }
    // IDLE: Stopping decides whether to program or, after testing, finish.
    step_ = Step::Stopping;
    tick();
    return;

  case Step::Connecting:
    if (tag_.IsAttached() || (attach_ && attach_())) {
      timer_.stop();
      beginTests();
      return;
    }
    if (++waitTicks_ >= kConnectTicks) {
      timer_.stop();
      setStep(Step::PowerCycle,
              retestOnly_
                  ? tr("The tag does not answer after its flash was read. Unplug the base, "
                       "plug it in again, and press Retry.")
              : image_.plan.erase == FlashPlan::Erase::All
                  ? tr("Programmed, but the tag does not answer. A mass erase can leave "
                       "it in its bootloader until it is powered off: unplug the base, "
                       "plug it in again, and press Retry.")
                  : tr("Programmed, but the tag does not answer. Unplug the base, plug "
                       "it in again, and press Retry."));
    }
    return;

  case Step::Testing:
    if (answered && state == TEST) {
      if (++waitTicks_ > kTestTicks)
        finish(QStringLiteral("TEST_FAILED"), tr("The self-tests did not finish."));
      return;
    }
    if (!answered) {
      finish(QStringLiteral("FAILED: the tag stopped answering"),
             tr("The tag stopped answering during its self-tests."));
      return;
    }
    result_.testStatus = QString::fromStdString(TestResult_Name(status.test_status()));
    // The self-tests disturb the tag's clock; set it again now they are done.
    tag_.SetRtc();
    if (retestOnly_) {
      const bool passed = result_.testStatus == QLatin1String("ALL_PASSED");
      QString flash;
      switch (result_.flash) {
      case SessionResult::Flash::Matches:
        flash = tr(" The flash matches %1.").arg(image_.displayName());
        break;
      case SessionResult::Flash::Differs:
        flash = tr(" The flash is not %1: %2").arg(image_.displayName(), result_.flashNote);
        break;
      case SessionResult::Flash::Unread:
        flash = tr(" The flash was not compared: %1.").arg(result_.flashNote);
        break;
      case SessionResult::Flash::NotChecked:
        flash = tr(" No downloaded image matches the tag's commit, so its flash was not "
                   "compared.");
        break;
      }
      finish(result_.flash == SessionResult::Flash::Differs ? QStringLiteral("MISMATCH")
             : passed                                       ? QStringLiteral("OK")
                                                            : QStringLiteral("TEST_FAILED"),
             tr("Self-tests: %1.").arg(result_.testStatus) + flash);
      return;
    }
    // Done. The programmer does not start the tag: a Start belongs to its
    // deployment, and on a PresTag it arms the stop-mode RTC ticker, which
    // leaves the RTC write-protected until the next reset (a SetRtc then
    // hangs the firmware; embedded/tags/TODO.md). The tag stays IDLE.
    {
      const bool passed = result_.testStatus == QLatin1String("ALL_PASSED");
      finish(mismatch_ ? QStringLiteral("MISMATCH")
             : passed  ? QStringLiteral("OK")
                       : QStringLiteral("TEST_FAILED"),
             mismatch_ ? tr("Programmed, but the tag reports commit %1, which is not the "
                            "image's. It is not running the image just written.")
                             .arg(result_.gitHash)
             : passed  ? tr("Programmed and tested: ALL_PASSED. The tag is IDLE.")
                       : tr("Programmed, but the self-test reported %1.")
                             .arg(result_.testStatus));
    }
    return;

  case Step::Programming:
  case Step::Reading:
  case Step::PowerCycle:
  case Step::Done:
    return;
  }
}
