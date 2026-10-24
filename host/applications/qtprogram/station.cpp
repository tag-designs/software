/**
 * @file    station.cpp
 * @brief   The station's checks, tag handling, firmware choice and record.
 */

#include "station.h"

#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>

namespace
{

// QSettings keys.
const char *kProgrammerKey = "programmer";
const char *kReleaseKey = "firmwareRelease";
const char *kFileKey = "firmwareFile";
const char *kLegacyImageKey = "lastImage";  // qtprogram 1.x
const char *kRecentKey = "recentFiles";
const char *kTargetKey = "target";
const char *kRecordKey = "recordPath";
const char *kBaseKey = "baseSerial";
const char *kAutoAttachKey = "resetsAcknowledged";
const char *kLabelKey = "lastLabel";

/// Recent files kept.
constexpr int kRecentCount = 6;
/// Absolute maximum VDD of the STM32L432, volts (datasheet).
constexpr double kL432MaxVolts = 3.6;
/// Below this the tag may brown out while programming, volts.
constexpr double kLowVolts = 1.8;
/// Polls the tag may miss before it is taken as gone.
constexpr int kMissedPolls = 5;
/// Attach attempts after the programmer's SWD check, and the wait between them.
constexpr int kAttachTries = 10;
constexpr int kAttachRetryMs = 200;

/// Last path component of a TagInfo.source_path, e.g. "BitTag".
QString targetOfPath(const QString &sourcePath)
{
  return sourcePath.section('/', -1, -1, QString::SectionSkipEmpty);
}

/// Whether two git hashes name the same commit, either being a prefix of the other.
bool sameCommit(const QString &a, const QString &b)
{
  return !a.isEmpty() && !b.isEmpty() &&
         (a.startsWith(b, Qt::CaseInsensitive) || b.startsWith(a, Qt::CaseInsensitive));
}

/// The CSV's erase column for @p plan.
QString eraseName(const FlashPlan &plan)
{
  switch (plan.erase) {
  case FlashPlan::Erase::None:
    return QStringLiteral("none");
  case FlashPlan::Erase::BelowCalibration:
    return QStringLiteral("below-calibration");
  case FlashPlan::Erase::All:
    return QStringLiteral("all");
  default:
    return QString();
  }
}

} // namespace

Station::Station(QObject *parent)
    : QObject(parent),
      catalog_(QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation))
                   .filePath("firmware")),
      session_(tag_, cli_)
{
  basePoll_.setInterval(1000);
  connect(&basePoll_, &QTimer::timeout, this, &Station::checkBase);
  statusPoll_.setInterval(400);
  connect(&statusPoll_, &QTimer::timeout, this, &Station::poll);
  connect(&session_, &ProgramSession::finished, this, &Station::writeRecord);

  connect(&catalog_, &FirmwareCatalog::releasesChanged, this, [this]() {
    // The chosen release may only now be listed, with an archive to fetch.
    if (!release_.isEmpty() && image_.elfPath.isEmpty() && !catalog_.fetching())
      loadImage();
    else
      emit firmwareChanged();
  });
  connect(&catalog_, &FirmwareCatalog::fetched, this, [this](const QString &tag) {
    if (tag == release_) {
      fetchError_.clear();
      loadImage();
    }
  });
  connect(&catalog_, &FirmwareCatalog::fetchFailed, this,
          [this](const QString &tag, const QString &why) {
            if (tag == release_) {
              fetchError_ = why;
              checkFirmware();
            }
          });

  QSettings settings;
  release_ = settings.value(kReleaseKey).toString();
  file_ = settings.value(kFileKey, settings.value(kLegacyImageKey)).toString();
  if (!release_.isEmpty())
    file_.clear();
  target_ = settings.value(kTargetKey).toString();
  label_ = settings.value(kLabelKey).toString();
}

Station::~Station()
{
  tag_.Detach();
}

QString Station::hostVersion()
{
  return QStringLiteral(QTPROGRAM_HOST_VERSION);
}

void Station::set(Check check, Level level, const QString &text)
{
  checks_[static_cast<int>(check)] = {level, text};
  emit checkChanged(check);
}

bool Station::autoAttach() const
{
  return QSettings().value(kAutoAttachKey, false).toBool();
}

void Station::setAutoAttach(bool on)
{
  QSettings().setValue(kAutoAttachKey, on);
  if (on && haveBase_ && !tag_.IsAttached())
    attach();
}

void Station::begin()
{
  checkProgrammer();
  checkBase();
  catalog_.refresh();
  loadImage();
  QString error;
  const QString path = QSettings().value(kRecordKey).toString();
  if (!path.isEmpty() && !record_.open(path, &error))
    qWarning().noquote() << error;
  checkRecord();
  if (!record_.isOpen())
    emit recordNeeded();
}

// ---- Programmer -------------------------------------------------------------

void Station::checkProgrammer()
{
  const QString path = findProgrammer(QSettings().value(kProgrammerKey).toString());
  if (path.isEmpty()) {
    set(Check::Programmer, Level::Fail,
        tr("STM32_Programmer_CLI not found. Choose the STM32CubeProgrammer folder."));
    return;
  }
  cli_.setPath(path);
  set(Check::Programmer, Level::Pending, tr("Checking %1").arg(path));
  cli_.version([this, path](int code, const QString &output) {
    const QString version = parseProgrammerVersion(output);
    if (code != 0 || version.isEmpty()) {
      set(Check::Programmer, Level::Fail,
          tr("%1 does not run. Choose the STM32CubeProgrammer folder.").arg(path));
      return;
    }
    QSettings().setValue(kProgrammerKey, path);
    set(Check::Programmer, Level::Ok, tr("STM32CubeProgrammer v%1").arg(version));
    if (haveBase_ && autoAttach() && status(Check::Swd).level == Level::Pending &&
        !tag_.IsAttached())
      attach();
  });
}

bool Station::setProgrammerDirectory(const QString &dir)
{
  const QString path = programmerInDirectory(dir);
  if (path.isEmpty())
    return false;
  QSettings().setValue(kProgrammerKey, path);
  checkProgrammer();
  return true;
}

// ---- Base and tag ----------------------------------------------------------

void Station::checkBase()
{
  if (session_.running() || tag_.IsAttached())
    return;  // enumerating opens the base to read its serial
  tag_.Available(bases_);
  if (bases_.empty()) {
    haveBase_ = false;
    set(Check::Base, Level::Warn, tr("No base on USB. Plug in the base."));
    basePoll_.start();
    return;
  }
  basePoll_.stop();
  const QString saved = QSettings().value(kBaseKey).toString();
  for (size_t i = 0; i < bases_.size(); ++i)
    if (bases_.size() == 1 || (!saved.isEmpty() && QString::fromStdString(bases_[i].serial) == saved)) {
      chooseBase(static_cast<int>(i));
      return;
    }
  set(Check::Base, Level::Warn, tr("%1 bases on USB: choose one.").arg(bases_.size()));
  emit baseChoiceNeeded();
}

void Station::chooseBase(int index)
{
  if (index < 0 || index >= static_cast<int>(bases_.size()))
    return;
  base_ = bases_[index];
  haveBase_ = true;
  serial_ = QString::fromStdString(base_.serial);
  if (!serial_.isEmpty()) {
    QSettings().setValue(kBaseKey, serial_);
    set(Check::Base, Level::Ok, tr("ST-LINK %1").arg(serial_));
    baseReady();
    return;
  }
  // Old ST-LINK/V2 firmware reports a binary serial; ask the programmer,
  // which can name it only when it is the only probe.
  set(Check::Base, Level::Pending, tr("Reading the ST-LINK serial"));
  cli_.listProbes([this](int code, const QString &output) {
    const QList<ProbeInfo> probes = parseProbeList(output);
    if (code != 0 || probes.size() != 1) {
      haveBase_ = false;
      set(Check::Base, Level::Fail,
          tr("Cannot read the base's ST-LINK serial; unplug the other bases."));
      return;
    }
    serial_ = probes.first().serial;
    set(Check::Base, Level::Ok, tr("ST-LINK %1").arg(serial_));
    baseReady();
  });
}

void Station::baseReady()
{
  if (autoAttach() && status(Check::Programmer).level == Level::Ok) {
    attach();
    return;
  }
  set(Check::Swd, Level::Pending, tr("Press Attach to connect (this resets the tag)."));
}

bool Station::reattach()
{
  tag_.Detach();
  tag_.Available(bases_);
  for (const UsbDev &dev : bases_)
    if (QString::fromStdString(dev.serial) == serial_ || bases_.size() == 1) {
      base_ = dev;
      return tag_.Attach(base_);
    }
  return false;
}

QString Station::tagTarget() const
{
  return tagAnswers() ? targetOfPath(QString::fromStdString(info_.source_path())) : QString();
}

void Station::forgetTag()
{
  statusPoll_.stop();
  tag_.Detach();
  haveInfo_ = false;
  info_.Clear();
  tagStatus_.Clear();
  missedPolls_ = 0;
  connect_ = ConnectInfo();
  if (targetDetected_)
    targetDetected_ = false;
  emit tagChanged();
}

void Station::attach()
{
  if (session_.running() || cli_.busy() || attaching_)
    return;
  forgetTag();
  if (status(Check::Programmer).level != Level::Ok) {
    set(Check::Swd, Level::Pending, tr("Waiting for STM32_Programmer_CLI."));
    return;
  }
  if (!haveBase_) {
    checkBase();
    if (!haveBase_)
      return;
  }
  // The base may have been replugged, with a new USB address; find it by serial.
  tag_.Available(bases_);
  const auto it = std::find_if(bases_.begin(), bases_.end(), [this](const UsbDev &d) {
    return QString::fromStdString(d.serial) == serial_ || bases_.size() == 1;
  });
  if (it == bases_.end()) {
    haveBase_ = false;
    set(Check::Base, Level::Warn, tr("The base is not on USB. Plug it in."));
    basePoll_.start();
    return;
  }
  base_ = *it;
  set(Check::Base, Level::Ok, tr("ST-LINK %1").arg(serial_));
  for (Check c : {Check::Supply, Check::Tag, Check::TagState})
    set(c, Level::Pending, QString());
  set(Check::Swd, Level::Pending, tr("Connecting over SWD"));
  cli_.connectCheck(serial_, [this](int code, const QString &output) {
    connect_ = parseConnect(output, code);
    if (!connect_.ok) {
      set(Check::Swd, Level::Fail,
          tr("%1. Is a tag in the base, and powered?").arg(connect_.error));
      checkSupply();
      checkFirmware();
      return;
    }
    set(Check::Swd, Level::Ok,
        tr("0x%1 %2").arg(connect_.deviceId, 3, 16, QChar('0')).arg(connect_.deviceName));
    checkSupply();
    attachTag(kAttachTries);
  });
}

void Station::attachTag(int tries)
{
  attaching_ = false;
  if (tag_.Attach(base_) && tag_.GetTagInfo(info_)) {
    haveInfo_ = true;
    set(Check::Tag, Level::Ok,
        tr("%1, %2").arg(QString::fromStdString(info_.board_desc()),
                         QString::fromStdString(info_.githash())));
    tag_.GetStatus(tagStatus_);
    checkTagState();
    detectTarget();
    statusPoll_.start();
  } else if (tries > 1 && !tag_.IsAttached()) {
    // The ST-LINK can still be held for a moment after the programmer exits
    // (on macOS the first open fails); nothing reached the tag, so try again.
    attaching_ = true;
    QTimer::singleShot(kAttachRetryMs, this, [this, tries]() { attachTag(tries - 1); });
    return;
  } else {
    tag_.Detach();
    set(Check::Tag, Level::Warn,
        tr("No firmware answers: a blank board, or one in its bootloader. Choose the "
           "tag type."));
    set(Check::TagState, Level::Pending, QString());
  }
  emit tagChanged();
  checkFirmware();
}

void Station::checkSupply()
{
  const double v = connect_.voltage;
  if (v < 0) {
    set(Check::Supply, Level::Pending, QString());
    return;
  }
  const QString volts = tr("%1 V").arg(v, 0, 'f', 2);
  if (connect_.deviceId == kDeviceIdL432 && v > kL432MaxVolts)
    set(Check::Supply, Level::Fail,
        tr("%1: above the STM32L432's %2 V maximum. Lower the supply.")
            .arg(volts).arg(kL432MaxVolts));
  else if (v < kLowVolts)
    set(Check::Supply, Level::Warn, tr("%1: low; programming may fail.").arg(volts));
  else
    set(Check::Supply, Level::Ok, volts);
}

void Station::checkTagState()
{
  const TagState s = tagStatus_.state();
  const QString name = QString::fromStdString(TagState_Name(s));
  switch (s) {
  case IDLE:
    set(Check::TagState, Level::Ok, name);
    break;
  case ABORTED:
  case FINISHED:
    set(Check::TagState, Level::Warn,
        tr("%1: holds a log, which programming erases. Download it with Tag Monitor "
           "first.").arg(name));
    break;
  case TEST:
  case sRESET:
    set(Check::TagState, Level::Pending, tr("%1 in progress").arg(name));
    break;
  default:
    set(Check::TagState, Level::Warn,
        tr("%1: deployed. Programming stops it and erases its log.").arg(name));
    break;
  }
}

void Station::poll()
{
  if (session_.running() || !tag_.IsAttached())
    return;
  Status status;
  if (!tag_.GetStatus(status)) {
    if (++missedPolls_ >= kMissedPolls) {
      forgetTag();
      set(Check::Tag, Level::Warn, tr("The tag stopped answering. Press Attach."));
      set(Check::TagState, Level::Pending, QString());
      set(Check::Swd, Level::Pending, tr("Press Attach to connect (this resets the tag)."));
    }
    return;
  }
  missedPolls_ = 0;
  const bool changed = status.state() != tagStatus_.state() ||
                       status.test_status() != tagStatus_.test_status() ||
                       status.sectors_erased() != tagStatus_.sectors_erased();
  tagStatus_ = status;
  if (changed) {
    checkTagState();
    emit tagChanged();
  }
}

void Station::nextTag()
{
  if (session_.running())
    return;
  forgetTag();
  set(Check::Swd, Level::Pending, tr("Swap the tag, then press Attach."));
  for (Check c : {Check::Supply, Check::Tag, Check::TagState})
    set(c, Level::Pending, QString());
  checkFirmware();
}

void Station::stopTag()
{
  if (tag_.IsAttached() && !session_.running())
    tag_.Stop();
}

void Station::eraseTag()
{
  if (tag_.IsAttached() && !session_.running() &&
      (tagStatus_.state() == FINISHED || tagStatus_.state() == ABORTED))
    tag_.Erase();
}

// ---- Firmware ---------------------------------------------------------------

QStringList Station::recentFiles() const
{
  return QSettings().value(kRecentKey).toStringList();
}

void Station::chooseRelease(const QString &tag)
{
  release_ = tag;
  file_.clear();
  fetchError_.clear();
  QSettings().setValue(kReleaseKey, tag);
  QSettings().remove(kFileKey);
  loadImage();
}

void Station::chooseFile(const QString &path)
{
  file_ = path;
  release_.clear();
  QSettings().setValue(kFileKey, path);
  QSettings().remove(kReleaseKey);
  QStringList recent = recentFiles();
  recent.removeAll(path);
  recent.prepend(path);
  QSettings().setValue(kRecentKey, QStringList(recent.mid(0, kRecentCount)));
  loadImage();
}

QStringList Station::targets() const
{
  if (!release_.isEmpty())
    return catalog_.targets(release_);
  return image_.identity.found ? QStringList{image_.identity.target} : QStringList();
}

void Station::chooseTarget(const QString &target)
{
  target_ = target;
  targetDetected_ = false;
  QSettings().setValue(kTargetKey, target);
  loadImage();
}

void Station::detectTarget()
{
  const QString running = tagTarget();
  if (running.isEmpty() || release_.isEmpty() || !targets().contains(running))
    return;
  target_ = running;
  targetDetected_ = true;
  QSettings().setValue(kTargetKey, running);
  loadImage();
}

void Station::loadImage()
{
  image_ = FirmwareImage();
  if (!file_.isEmpty()) {
    image_ = loadFirmwareImage(file_);
    if (image_.identity.found)
      target_ = image_.identity.target;
  } else if (!release_.isEmpty()) {
    if (!catalog_.isCached(release_)) {
      if (!catalog_.release(release_).archiveUrl.isEmpty())
        catalog_.fetch(release_);
    } else if (targets().contains(target_)) {
      image_ = loadFirmwareImage(catalog_.imagePath(release_, target_), release_);
    }
  }
  checkFirmware();
  emit firmwareChanged();
}

void Station::checkFirmware()
{
  if (release_.isEmpty() && file_.isEmpty()) {
    set(Check::Firmware, Level::Fail, tr("Choose the firmware."));
    return;
  }
  if (!release_.isEmpty() && !catalog_.isCached(release_)) {
    if (!fetchError_.isEmpty())
      set(Check::Firmware, Level::Fail, tr("%1 could not be downloaded: %2")
                                            .arg(release_, fetchError_));
    else if (catalog_.fetching())
      set(Check::Firmware, Level::Pending, tr("Downloading %1").arg(release_));
    else if (catalog_.refreshing())
      set(Check::Firmware, Level::Pending, tr("Looking up %1 on GitHub").arg(release_));
    else if (!catalog_.refreshError().isEmpty())
      set(Check::Firmware, Level::Fail, tr("%1 is not downloaded, and GitHub cannot be "
                                           "reached: %2").arg(release_, catalog_.refreshError()));
    else
      set(Check::Firmware, Level::Fail, tr("%1 is not a published release.").arg(release_));
    return;
  }
  if (!release_.isEmpty() && !targets().contains(target_)) {
    set(Check::Firmware, Level::Fail,
        target_.isEmpty() ? tr("Choose the tag type.")
                          : tr("%1 has no image for %2.").arg(release_, target_));
    return;
  }
  if (!image_.usable()) {
    set(Check::Firmware, Level::Fail, image_.error);
    return;
  }
  const bool imageU375 = image_.plan.pageSize == 4096;
  if (connect_.deviceId != 0 && imageU375 != (connect_.deviceId == kDeviceIdU375)) {
    set(Check::Firmware, Level::Fail,
        tr("This image is for an %1, and the tag is an %2.")
            .arg(imageU375 ? "STM32U375" : "STM32L432",
                 connect_.deviceId == kDeviceIdU375 ? "STM32U375" : "STM32L432"));
    return;
  }
  const QString running = tagTarget();
  if (!running.isEmpty() && image_.identity.found && running != image_.identity.target) {
    set(Check::Firmware, Level::Warn,
        tr("The image is for %1, and the tag runs %2.").arg(image_.identity.target, running));
    return;
  }
  if (!image_.warning.isEmpty()) {
    set(Check::Firmware, Level::Warn, image_.warning);
    return;
  }
  const QString unqualified = qualificationWarning();
  if (!unqualified.isEmpty()) {
    set(Check::Firmware, Level::Warn, unqualified);
    return;
  }
  set(Check::Firmware, Level::Ok,
      image_.manifestVerified ? tr("%1, verified against its manifest").arg(image_.displayName())
                              : tr("%1, no manifest to check").arg(image_.displayName()));
}

QString Station::qualificationWarning() const
{
  if (!file_.isEmpty())
    return tr("%1 is a local file, not a qualified release.")
        .arg(QFileInfo(file_).fileName());
  if (release_.isEmpty() || target_.isEmpty())
    return QString();
  const FirmwareRelease r = catalog_.release(release_);
  if (!r.listed)
    return tr("Cannot tell whether %1 is qualified for %2: GitHub has not been reached.")
        .arg(release_, target_);
  if (!r.isQualified(target_))
    return tr("%1 is not qualified for %2. See the Release Notes.").arg(release_, target_);
  return QString();
}

// ---- Record -----------------------------------------------------------------

QString Station::defaultRecordPath()
{
  return QDir(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation))
      .filePath("Tag Programmer/programmed-tags.csv");
}

bool Station::openRecord(const QString &path, QString *error)
{
  const bool ok = record_.open(path, error);
  if (ok)
    QSettings().setValue(kRecordKey, path);
  checkRecord();
  return ok;
}

bool Station::createRecord(const QString &path, QString *error)
{
  QDir().mkpath(QFileInfo(path).absolutePath());
  const bool ok = record_.create(path, error);
  if (ok)
    QSettings().setValue(kRecordKey, path);
  checkRecord();
  return ok;
}

void Station::checkRecord()
{
  QString error;
  if (!record_.isOpen())
    set(Check::Record, Level::Fail, tr("No record file. Create or choose one."));
  else if (!record_.writable(&error))
    set(Check::Record, Level::Fail, error);
  else {
    const QString name = QFileInfo(record_.path()).fileName();
    set(Check::Record, Level::Ok,
        record_.count() == 1 ? tr("%1 (1 row)").arg(name)
                             : tr("%1 (%2 rows)").arg(name).arg(record_.count()));
  }
}

// ---- Programming ------------------------------------------------------------

bool Station::canProgram(QString *why) const
{
  auto blocked = [why](const QString &text) {
    if (why)
      *why = text;
    return false;
  };
  if (session_.running())
    return blocked(tr("Programming is in progress."));
  if (attaching_ || cli_.busy())
    return blocked(tr("Connecting to the tag."));
  for (Check c : {Check::Programmer, Check::Base, Check::Swd, Check::Record})
    if (status(c).level != Level::Ok)
      return blocked(status(c).text);
  if (status(Check::Supply).level == Level::Fail)
    return blocked(status(Check::Supply).text);
  if (status(Check::Firmware).level == Level::Fail ||
      status(Check::Firmware).level == Level::Pending || !image_.usable())
    return blocked(status(Check::Firmware).text);
  return true;
}

void Station::program(const QString &label)
{
  checkRecord();
  if (!canProgram(nullptr))
    return;
  label_ = label;
  QSettings().setValue(kLabelKey, label);
  sessionImage_ = image_;
  sessionVoltage_ = connect_.voltage;
  sessionRetest_ = false;
  statusPoll_.stop();
  session_.start(image_, serial_, [this]() { return reattach(); });
}

void Station::retest(const QString &label)
{
  checkRecord();
  if (session_.running() || status(Check::Record).level != Level::Ok)
    return;
  label_ = label;
  QSettings().setValue(kLabelKey, label);
  // Record the image only when the tag is known to run it.
  const QString running = QString::fromStdString(info_.githash());
  sessionImage_ = haveInfo_ && sameCommit(running, image_.identity.gitHash) ? image_
                                                                            : FirmwareImage();
  sessionVoltage_ = connect_.voltage;
  sessionRetest_ = true;
  statusPoll_.stop();
  session_.retest([this]() { return reattach(); });
}

void Station::writeRecord(const SessionResult &result)
{
  // The tag is left attached when it answers; show it again.
  haveInfo_ = tag_.IsAttached() && tag_.GetTagInfo(info_);
  if (haveInfo_) {
    tag_.GetStatus(tagStatus_);
    checkTagState();
    statusPoll_.start();
  }
  emit tagChanged();

  if (!result.programmed && !sessionRetest_) {
    // Nothing was written to the tag: no row (the proposal's step 7).
    emit programFinished(ProgramRecord(), false, result.message);
    return;
  }
  ProgramRecord row;
  row.label = label_;
  row.tagType = sessionImage_.identity.found ? sessionImage_.identity.target : target_;
  row.uuid = !result.uuid.isEmpty() ? result.uuid : QString::fromStdString(info_.uuid());
  row.firmwareVersion = sessionImage_.elfPath.isEmpty() ? QString() : sessionImage_.version();
  row.gitHash = !result.gitHash.isEmpty() ? result.gitHash : sessionImage_.identity.gitHash;
  row.date = QDateTime::currentDateTimeUtc();
  row.testStatus = result.testStatus;
  row.result = result.result;
  row.imageSha256 = sessionImage_.sha256;
  row.image = sessionImage_.elfPath.isEmpty() ? QString()
                                              : QFileInfo(sessionImage_.elfPath).fileName();
  row.erase = sessionRetest_ ? QString() : eraseName(sessionImage_.plan);
  row.voltage = sessionVoltage_;
  row.probeSerial = serial_;
  row.hostVersion = hostVersion();

  QString error;
  const bool written = record_.append(row, &error);
  if (written) {
    if (row.result == QLatin1String("OK"))
      ++sessionOk_;
    else
      ++sessionFailed_;
  } else {
    qCritical().noquote() << "The record was NOT written:" << error << "Row:"
                          << row.fields().join(',');
  }
  checkRecord();
  emit programFinished(row, written, written ? result.message
                                             : tr("%1\n\nThe record was NOT written: %2. "
                                                  "The row is in the log.")
                                                   .arg(result.message, error));
}
